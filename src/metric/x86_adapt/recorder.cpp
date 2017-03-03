#include <lo2s/metric/x86_adapt/recorder.hpp>

#include <lo2s/trace/trace.hpp>

namespace lo2s
{
namespace metric
{
namespace x86_adapt
{
Recorder::Recorder(::x86_adapt::device device, std::chrono::nanoseconds sampling_interval,
                   const std::vector<::x86_adapt::configuration_item>& configuration_items,
                   trace::Trace& trace, const otf2::definition::metric_class& metric_class)
: device_(std::move(device)), sampling_interval_(sampling_interval),
  writer_(
      trace.metric_writer((boost::format("x86_adapt metrics for CPU %d") % device_.id()).str())),
  metric_instance_(trace.metric_instance(metric_class, writer_.location(),
                                         trace.system_tree_cpu_node(device_.id()))),
  configuration_items_(configuration_items)
// (WOW this is (almost) better than LISP)
{
    assert(device_.type() == X86_ADAPT_CPU);
}

void Recorder::loop()
{
    cpu_set_t cpumask;
    CPU_ZERO(&cpumask);
    CPU_SET(device_.id(), &cpumask);
    sched_setaffinity(0, sizeof(cpumask), &cpumask);

    std::vector<otf2::event::metric::value_container> values(configuration_items_.size());

    for (std::size_t i = 0; i < configuration_items_.size(); i++)
    {
        values[i].metric = metric_instance_.metric_class()[i];
    }

    while (looping_.load())
    {
        auto read_time = time::now();
        for (const auto& index_ci : nitro::lang::enumerate(configuration_items_))
        {
            values[index_ci.index()].set(device_(index_ci.value()));
        }

        // TODO optimize! (avoid copy, avoid shared pointers...)
        writer_.write(otf2::event::metric(read_time, metric_instance_, values));

        std::this_thread::sleep_for(sampling_interval_);
    }
}

void Recorder::start()
{
    looping_.store(true);

    cpu_set_t cpumask;
    sched_getaffinity(0, sizeof(cpu_set_t), &cpumask);

    thread_ = std::make_unique<std::thread>([this]() { this->loop(); });
    running_ = true;
}

void Recorder::stop()
{
    if (!running_)
        return;

    looping_.store(false);

    thread_->join();

    thread_.reset(nullptr);

    running_ = false;
}
}
}
}
