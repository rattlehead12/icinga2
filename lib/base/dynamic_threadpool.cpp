/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2017 Icinga Development Team (https://www.icinga.com/)  *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "base/dynamic_threadpool.hpp"
#include <boost/bind.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/chrono.hpp>

using namespace icinga;

CallableContainer::CallableContainer()
{
}

CallableContainer::~CallableContainer()
{
}

void CallableContainer::operator() (void)
{
}

DynamicThreadPool::DynamicThreadPool()
{
}

DynamicThreadPool::~DynamicThreadPool()
{
	boost::mutex::scoped_lock lock(m_Mutex);
	while (m_TaskChannel.PushFillOrKill(NULL))
		;

	while (!m_Threads.empty()) {
		while (m_Threads.top().joinable() && !m_Threads.top().try_join_for(boost::chrono::microseconds(1)))
			m_TaskChannel.PushFillOrKill(NULL);

		m_Threads.pop();
	}
}

void DynamicThreadPool::PostImpl(CallableContainer *task)
{
	boost::mutex::scoped_lock lock(m_Mutex);
	if (m_TaskChannel.PushFillOrKill(task))
		return;

	m_Threads.emplace(boost::bind(&DynamicThreadPool::DoTasks, this));
	m_TaskChannel.Push(task);
}

void DynamicThreadPool::DoTasks()
{
	for (CallableContainer *task;;) {
		task = m_TaskChannel.Pop();
		if (task == NULL)
			break;

		try {
			(*task)();
		} catch (...) {
			delete task;
			throw;
		}

		delete task;
	}
}

