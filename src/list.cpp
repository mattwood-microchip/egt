/*
 * Copyright (C) 2018 Microchip Technology Inc.  All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "egt/detail/imagecache.h"
#include "egt/frame.h"
#include "egt/input.h"
#include "egt/list.h"
#include "egt/painter.h"

using namespace std;

namespace egt
{
inline namespace v1
{

ListBox::ListBox(const item_array& items) noexcept
    : ListBox(items, Rect())
{
}

ListBox::ListBox(const Rect& rect) noexcept
    : ListBox(item_array(), rect)
{
}

ListBox::ListBox(const item_array& items, const Rect& rect) noexcept
    : Frame(rect),
      m_view(make_shared<ScrolledView>(*this, rect, orientation::vertical)),
      m_sizer(make_shared<OrientationPositioner>(*m_view.get(), orientation::vertical))
{
    set_name("ListBox" + std::to_string(m_widgetid));

    set_boxtype(Theme::boxtype::borderfill);

    m_view->set_align(alignmask::expand);

    m_sizer->set_align(alignmask::expand);

    for (auto& i : items)
        add_item_private(i);
}

ListBox:: ListBox(Frame& parent, const item_array& items) noexcept
    : ListBox(items)
{
    parent.add(*this);
}

ListBox::ListBox(Frame& parent, const item_array& items, const Rect& rect) noexcept
    : ListBox(items, rect)
{
    parent.add(*this);
}

void ListBox::add_item(const std::shared_ptr<Widget>& widget)
{
    add_item_private(widget);
}

void ListBox::add_item_private(const std::shared_ptr<Widget>& widget)
{
    m_sizer->add(widget);

    widget->resize(Size(0, item_height()));
    widget->set_align(alignmask::expand_horizontal);

    if (m_sizer->count_children() == 1)
    {
        m_selected = 0;
        m_sizer->child_at(m_selected)->set_active(true);
    }
}

Widget* ListBox::get_item(uint32_t index)
{
    return m_sizer->child_at(index);
}

void ListBox::remove_item(Widget* widget)
{
    m_sizer->remove(widget);

    if (m_selected >= m_sizer->count_children())
    {
        if (m_sizer->count_children())
            set_select(m_sizer->count_children() - 1);
        else
            m_selected = 0;
    }
}

Rect ListBox::item_rect(uint32_t index) const
{
    Rect r(box());
    r.h = item_height();
    r.y += (r.h * index);
    r.y += m_view->offset();
    return r;
}

int ListBox::handle(eventid event)
{
    auto ret = Frame::handle(event);
    if (ret)
        return ret;

    switch (event)
    {
    case eventid::pointer_click:
    {
        Point mouse = from_display(event::pointer().point);
        for (size_t i = 0; i < m_sizer->count_children(); i++)
        {
            if (Rect::point_inside(mouse, item_rect(i)))
            {
                set_select(i);
                break;
            }
        }

        return 1;
    }
    default:
        break;
    }

    return ret;
}

void ListBox::set_select(uint32_t index)
{
    if (m_selected != index)
    {
        if (index < m_sizer->count_children())
        {
            if (m_sizer->count_children() > m_selected)
                m_sizer->child_at(m_selected)->set_active(false);
            m_selected = index;
            if (m_sizer->count_children() > m_selected)
                m_sizer->child_at(m_selected)->set_active(true);

            damage();
            invoke_handlers(eventid::property_changed);
        }
    }
}

void ListBox::clear()
{
    m_sizer->remove_all();
}

Rect ListBox::child_area() const
{
    auto b = box() - Size(Theme::DEFAULT_BORDER_WIDTH * 2, Theme::DEFAULT_BORDER_WIDTH * 2);
    b += Point(Theme::DEFAULT_BORDER_WIDTH, Theme::DEFAULT_BORDER_WIDTH);
    return b;
}

ListBox::~ListBox()
{
}

}
}
