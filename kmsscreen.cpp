/*
 * Copyright (C) 2018 Microchip Technology Inc.  All rights reserved.
 * Joshua Henderson <joshua.henderson@microchip.com>
 */
#include <thread>

#ifdef HAVE_LIBPLANES

#include "event_loop.h"
#include "input.h"
#include "kmsscreen.h"
#include "planes/fb.h"
#include "planes/kms.h"
#include "planes/plane.h"
#include "widget.h"
#include "window.h"
#include <cairo.h>
#include <drm_fourcc.h>
#include <xf86drm.h>


#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>

using namespace std;

namespace mui
{
    static KMSScreen* the_kms = 0;

    static const uint32_t NUM_OVERLAY_BUFFERS = 3;

    KMSOverlayScreen::KMSOverlayScreen(struct plane_data* plane)
	: m_plane(plane),
	  m_index(0)
    {
	init(plane->bufs, NUM_OVERLAY_BUFFERS,
	     plane_width(plane), plane_height(plane));
    }

    void* KMSOverlayScreen::raw()
    {
	return m_plane->bufs[index()];
    }

    struct FlipThread
    {
	FlipThread()
	    : m_stop(false)
	{
	    m_thread = std::thread(&FlipThread::run, this);
	}

	void run()
	{
	    std::function<void()> task;
	    while(true)
	    {
		{
		    std::unique_lock<std::mutex> lock(m_mutex);

		    while(!m_stop && m_queue.empty())
			m_condition.wait(lock);

		    if (m_stop)
			return;

		    task = m_queue.front();
		    m_queue.pop_front();
		}

		task();
	    }
	}

	void enqueue(function<void()> job)
	{
	    {
		unique_lock<mutex> lock(m_mutex);
		m_queue.push_back(job);

		while (m_queue.size() > 1)
		{
		    cout << "too many flip jobs queued" << endl;
		    m_queue.pop_front();
		}
	    }
	    m_condition.notify_one();
	}

	~FlipThread()
	{
	    m_stop = true;
	    m_condition.notify_all();
	    m_thread.join();
	}

	std::thread m_thread;
	std::deque<function<void()>> m_queue;
	std::mutex m_mutex;
	std::condition_variable m_condition;
	bool m_stop;
    };

    struct FlipJob
    {
	explicit FlipJob(struct plane_data* plane, uint32_t index)
	    : m_plane(plane), m_index(index)
	{}

	void operator()()
	{
	    plane_flip(m_plane, m_index);
	}

	struct plane_data* m_plane;
	uint32_t m_index;
    };

    void KMSOverlayScreen::schedule_flip()
    {
#if 1
	static FlipThread pool;
	pool.enqueue(FlipJob(m_plane, m_index));
#else
	plane_flip(m_plane, m_index);
#endif

	if (++m_index >= m_plane->buffer_count)
	    m_index = 0;
    }

    uint32_t KMSOverlayScreen::index()
    {
	return m_index;
    }

    void KMSOverlayScreen::position(int x, int y)
    {
	plane_set_pos(m_plane, x, y);
    }

    void KMSOverlayScreen::scale(float scale)
    {
	plane_set_scale(m_plane, scale);
    }

    float KMSOverlayScreen::scale() const
    {
	return m_plane->scale;
    }

    int KMSOverlayScreen::gem()
    {
	// TODO: array
	return m_plane->gem_names[0];
    }

    void KMSOverlayScreen::apply()
    {
	plane_apply(m_plane);
    }

    KMSOverlayScreen::~KMSOverlayScreen()
    {
    }

    KMSScreen::KMSScreen(bool primary)
	: m_index(0)
    {
	m_fd = drmOpen("atmel-hlcdc", NULL);
	assert(m_fd >= 0);

	m_device = kms_device_open(m_fd);
	assert(m_device);

	//kms_device_dump(m_device);

	if (primary)
	{
	    static const uint32_t NUM_PRIMARY_BUFFERS = 3;

	    m_plane = plane_create2(m_device,
				    DRM_PLANE_TYPE_PRIMARY,
				    0,
				    m_device->screens[0]->width,
				    m_device->screens[0]->height,
				    DRM_FORMAT_XRGB8888,
				    NUM_PRIMARY_BUFFERS);

	    assert(m_plane);
	    plane_fb_map(m_plane);

	    plane_apply(m_plane);

	    DBG("primary plane dumb buffer " << plane_width(m_plane) << "," <<
		plane_height(m_plane));

	    init(m_plane->bufs, NUM_PRIMARY_BUFFERS,
		 plane_width(m_plane), plane_height(m_plane));
	}
	else
	{
	    m_size = Size(m_device->screens[0]->width,
			  m_device->screens[0]->height);
	}

	the_kms = this;
    }

    void KMSScreen::schedule_flip()
    {
#if 1
	static FlipThread pool;
	pool.enqueue(FlipJob(m_plane, m_index));
#else
	plane_flip(m_plane, m_index);
#endif
	if (++m_index >= m_plane->buffer_count)
	    m_index = 0;
    }

    uint32_t KMSScreen::index()
    {
	return m_index;
    }

    KMSScreen* KMSScreen::instance()
    {
	return the_kms;
    }

    struct plane_data* KMSScreen::allocate_overlay(const Size& size, uint32_t format)
    {
	struct plane_data* plane = 0;

	static vector<int> used;

	// brute force: find something that will work
	for (int index = 0; index < 3; index++)
	{
	    if (find(used.begin(), used.end(), index) != used.end())
		continue;

	    plane = plane_create2(m_device,
				  DRM_PLANE_TYPE_OVERLAY,
				  index,
				  size.w,
				  size.h,
				  format,
				  NUM_OVERLAY_BUFFERS);
	    if (plane)
	    {
		used.push_back(index);
		break;
	    }
	}

	assert(plane);
	plane_fb_map(plane);

	plane_set_pos(plane, 0, 0);

	cout << "plane " << plane->index << " overlay dumb buffer " <<
	    plane_width(plane) << "," << plane_height(plane) << endl;

	return plane;
    }

    uint32_t KMSScreen::count_planes(int type)
    {
	uint32_t total = 0;
	for (uint32_t x = 0; x < m_device->num_planes;x++)
	{
	    if ((int)m_device->planes[x]->type == type)
		total++;
	}
	return total;
    }

    KMSScreen::~KMSScreen()
    {
	kms_device_close(m_device);
	drmClose(m_fd);
    }

}
#endif
