/*
 * This file is part of the lo2s software.
 * Linux OTF2 sampling
 *
 * Copyright (c) 2016,
 *    Technische Universitaet Dresden, Germany
 *
 * lo2s is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * lo2s is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with lo2s.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <lo2s/trace/counters.hpp>

#include <lo2s/config.hpp>
#include <lo2s/log.hpp>
#include <lo2s/perf/event_provider.hpp>
#include <lo2s/platform.hpp>

namespace
{
static std::vector<lo2s::perf::CounterDescription> collect_counters()
{

    const auto& mem_events = lo2s::platform::get_mem_events();
    const auto& user_events = lo2s::config().perf_events;

    std::vector<lo2s::perf::CounterDescription> used_counters;

    used_counters.reserve(mem_events.size() + user_events.size());
    for (const auto& ev : user_events)
    {
        try
        {
            const auto event_desc = lo2s::perf::EventProvider::get_event_by_name(ev);
            used_counters.emplace_back(event_desc);
        }
        catch (const lo2s::perf::EventProvider::InvalidEvent& e)
        {
            lo2s::Log::warn() << "'" << ev
                              << "' does not name a known event, ignoring! (reason: " << e.what()
                              << ")";
        }
    }

    if (user_events.size() == 0)
    {
        for (const auto& description : mem_events)
        {
            used_counters.emplace_back(description);
        }

        used_counters.emplace_back(lo2s::perf::EventProvider::get_event_by_name("instructions"));
        used_counters.emplace_back(lo2s::perf::EventProvider::get_event_by_name("cpu-cycles"));
    }

    return used_counters;
}
}

namespace lo2s
{
namespace trace
{
// TODO This is an interdependent ball of ... please clean this up
Counters::Counters(pid_t pid, pid_t tid, Trace& trace_, otf2::definition::metric_class metric_class,
                   otf2::definition::location scope)
: writer_(trace_.metric_writer(pid, tid)),
  metric_instance_(trace_.metric_instance(metric_class, writer_.location(), scope)),
  counters_(tid, collect_counters()),
  proc_stat_(boost::filesystem::path("/proc") / std::to_string(pid) / "task" / std::to_string(tid) /
             "stat")
{
    auto mc = metric_instance_.metric_class();

    assert(counters_.size() <= mc.size());

    values_.resize(mc.size());
    for (std::size_t i = 0; i < mc.size(); i++)
    {
        values_[i].metric = mc[i];
    }
}

otf2::definition::metric_class Counters::get_metric_class(Trace& trace_)
{
    auto c = trace_.metric_class();
    const auto& user_events = lo2s::config().perf_events;

    for (const auto& ev : user_events)
    {
        if (perf::EventProvider::has_event(ev))
        {
            c.add_member(trace_.metric_member(ev, ev, otf2::common::metric_mode::accumulated_start,
                                              otf2::common::type::Double, "#"));
        }
    }

    if (user_events.size() == 0)
    {
        for (const auto& description : platform::get_mem_events())
        {
            c.add_member(trace_.metric_member(description.name, description.name,
                                              otf2::common::metric_mode::accumulated_start,
                                              otf2::common::type::Double, "#"));
        }

        c.add_member(trace_.metric_member("instructions", "instructions",
                                          otf2::common::metric_mode::accumulated_start,
                                          otf2::common::type::Double, "#"));
        c.add_member(trace_.metric_member("cycles", "CPU cycles",
                                          otf2::common::metric_mode::accumulated_start,
                                          otf2::common::type::Double, "#"));
    }

    c.add_member(trace_.metric_member("CPU", "CPU executing the task",
                                      otf2::common::metric_mode::absolute_last,
                                      otf2::common::type::int64, "cpuid"));
    c.add_member(trace_.metric_member("time_enabled", "time event active",
                                      otf2::common::metric_mode::accumulated_start,
                                      otf2::common::type::uint64, "ns"));
    c.add_member(trace_.metric_member("time_running", "time event on CPU",
                                      otf2::common::metric_mode::accumulated_start,
                                      otf2::common::type::uint64, "ns"));
    return c;
}

void Counters::write()
{
    auto read_time = time::now();

    assert(counters_.size() <= values_.size());

    counters_.read();
    for (std::size_t i = 0; i < counters_.size(); i++)
    {
        values_[i].set(counters_[i]);
    }
    auto index = counters_.size();
    values_[index++].set(get_task_last_cpu_id(proc_stat_));
    values_[index++].set(counters_.enabled());
    values_[index].set(counters_.running());

    // TODO optimize! (avoid copy, avoid shared pointers...)
    writer_.write(otf2::event::metric(read_time, metric_instance_, values_));
}
}
}
