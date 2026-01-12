#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace chmicro {

struct MetricLabels {
    // Labels are stored as a stable, sorted map to make exposition deterministic.
    std::map<std::string, std::string> kv;

    std::string ToPrometheusLabelText() const;
};

class Counter {
public:
    // Thread-safe
    void Inc(std::int64_t v = 1) { value_.fetch_add(v, std::memory_order_relaxed); }
    std::int64_t Value() const { return value_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::int64_t> value_{0};
};

class Gauge {
public:
    // Thread-safe
    void Set(double v);
    double Value() const;

private:
    mutable std::mutex mu_;
    double value_{0.0};
};

class Histogram {
public:
    // Thread-safe
    explicit Histogram(std::vector<double> buckets);

    void Observe(double v);
    std::vector<double> Buckets() const;

    // Returns counts per bucket (same size as buckets), and sum/total.
    void Snapshot(std::vector<std::uint64_t>& bucket_counts, double& sum, std::uint64_t& count) const;

private:
    std::vector<double> buckets_;
    mutable std::mutex mu_;
    std::vector<std::uint64_t> bucket_counts_;
    double sum_{0.0};
    std::uint64_t count_{0};
};

class MetricsRegistry {
public:
    // Thread-safe
    Counter& CounterMetric(std::string name, std::string help, MetricLabels labels = {});
    Gauge& GaugeMetric(std::string name, std::string help, MetricLabels labels = {});
    Histogram& HistogramMetric(std::string name, std::string help, std::vector<double> buckets, MetricLabels labels = {});

    // Thread-safe
    std::string ToPrometheusText() const;

private:
    struct CounterEntry {
        std::string help;
        MetricLabels labels;
        Counter counter;

        CounterEntry(std::string help_, MetricLabels labels_)
            : help(std::move(help_)), labels(std::move(labels_)), counter() {}
    };

    struct GaugeEntry {
        std::string help;
        MetricLabels labels;
        Gauge gauge;

        GaugeEntry(std::string help_, MetricLabels labels_)
            : help(std::move(help_)), labels(std::move(labels_)), gauge() {}
    };

    struct HistogramEntry {
        std::string help;
        MetricLabels labels;
        Histogram histogram;

        HistogramEntry(std::string help_, MetricLabels labels_, std::vector<double> buckets_)
            : help(std::move(help_)), labels(std::move(labels_)), histogram(std::move(buckets_)) {}
    };

    static std::string Key(std::string_view name, const MetricLabels& labels);

    mutable std::mutex mu_;
    std::unordered_map<std::string, CounterEntry> counters_;
    std::unordered_map<std::string, GaugeEntry> gauges_;
    std::unordered_map<std::string, HistogramEntry> histograms_;
};

// Global default registry (Thread-safe)
MetricsRegistry& DefaultMetrics();

} // namespace chmicro
