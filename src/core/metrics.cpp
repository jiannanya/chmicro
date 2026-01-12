#include <chmicro/core/metrics.h>

#include <algorithm>
#include <sstream>

namespace chmicro {

std::string MetricLabels::ToPrometheusLabelText() const {
    if (kv.empty()) {
        return {};
    }
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& it : kv) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << it.first << "=\"";
        for (char c : it.second) {
            if (c == '\\' || c == '"') {
                oss << '\\' << c;
            } else if (c == '\n') {
                oss << "\\n";
            } else {
                oss << c;
            }
        }
        oss << "\"";
    }
    oss << "}";
    return oss.str();
}

void Gauge::Set(double v) {
    std::lock_guard<std::mutex> lk(mu_);
    value_ = v;
}

double Gauge::Value() const {
    std::lock_guard<std::mutex> lk(mu_);
    return value_;
}

Histogram::Histogram(std::vector<double> buckets)
    : buckets_(std::move(buckets)), bucket_counts_(buckets_.size(), 0) {
    std::sort(buckets_.begin(), buckets_.end());
}

void Histogram::Observe(double v) {
    std::lock_guard<std::mutex> lk(mu_);
    sum_ += v;
    ++count_;

    auto it = std::lower_bound(buckets_.begin(), buckets_.end(), v);
    if (it != buckets_.end()) {
        size_t idx = static_cast<size_t>(it - buckets_.begin());
        ++bucket_counts_[idx];
    }
}

std::vector<double> Histogram::Buckets() const {
    std::lock_guard<std::mutex> lk(mu_);
    return buckets_;
}

void Histogram::Snapshot(std::vector<std::uint64_t>& bucket_counts, double& sum, std::uint64_t& count) const {
    std::lock_guard<std::mutex> lk(mu_);
    bucket_counts = bucket_counts_;
    sum = sum_;
    count = count_;
}

std::string MetricsRegistry::Key(std::string_view name, const MetricLabels& labels) {
    std::string key(name);
    key.push_back('\n');
    for (const auto& it : labels.kv) {
        key.append(it.first);
        key.push_back('=');
        key.append(it.second);
        key.push_back('\n');
    }
    return key;
}

Counter& MetricsRegistry::CounterMetric(std::string name, std::string help, MetricLabels labels) {
    std::lock_guard<std::mutex> lk(mu_);
    auto key = Key(name, labels);
    auto it = counters_.find(key);
    if (it == counters_.end()) {
        it = counters_.try_emplace(std::move(key), std::move(help), std::move(labels)).first;
    }
    return it->second.counter;
}

Gauge& MetricsRegistry::GaugeMetric(std::string name, std::string help, MetricLabels labels) {
    std::lock_guard<std::mutex> lk(mu_);
    auto key = Key(name, labels);
    auto it = gauges_.find(key);
    if (it == gauges_.end()) {
        it = gauges_.try_emplace(std::move(key), std::move(help), std::move(labels)).first;
    }
    return it->second.gauge;
}

Histogram& MetricsRegistry::HistogramMetric(std::string name, std::string help, std::vector<double> buckets, MetricLabels labels) {
    std::lock_guard<std::mutex> lk(mu_);
    auto key = Key(name, labels);
    auto it = histograms_.find(key);
    if (it == histograms_.end()) {
        it = histograms_.try_emplace(std::move(key), std::move(help), std::move(labels), std::move(buckets)).first;
    }
    return it->second.histogram;
}

std::string MetricsRegistry::ToPrometheusText() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::ostringstream oss;

    // Counters
    for (const auto& kv : counters_) {
        // We store the help/name only inside the key, so reconstruct by parsing is not worth it.
        // Instead, emit using stored fields.
        // Note: Prometheus type is inferred by suffix; we still emit TYPE.
        const auto& entry = kv.second;
        // Extract name from key prefix (until first '\n')
        auto pos = kv.first.find('\n');
        std::string name = (pos == std::string::npos) ? kv.first : kv.first.substr(0, pos);

        oss << "# HELP " << name << " " << entry.help << "\n";
        oss << "# TYPE " << name << " counter\n";
        oss << name << entry.labels.ToPrometheusLabelText() << " " << entry.counter.Value() << "\n";
    }

    // Gauges
    for (const auto& kv : gauges_) {
        const auto& entry = kv.second;
        auto pos = kv.first.find('\n');
        std::string name = (pos == std::string::npos) ? kv.first : kv.first.substr(0, pos);

        oss << "# HELP " << name << " " << entry.help << "\n";
        oss << "# TYPE " << name << " gauge\n";
        oss << name << entry.labels.ToPrometheusLabelText() << " " << entry.gauge.Value() << "\n";
    }

    // Histograms
    for (const auto& kv : histograms_) {
        const auto& entry = kv.second;
        auto pos = kv.first.find('\n');
        std::string name = (pos == std::string::npos) ? kv.first : kv.first.substr(0, pos);

        std::vector<std::uint64_t> bucket_counts;
        double sum = 0.0;
        std::uint64_t count = 0;
        entry.histogram.Snapshot(bucket_counts, sum, count);
        auto buckets = entry.histogram.Buckets();

        oss << "# HELP " << name << " " << entry.help << "\n";
        oss << "# TYPE " << name << " histogram\n";

        std::uint64_t cumulative = 0;
        for (size_t i = 0; i < buckets.size(); ++i) {
            cumulative += bucket_counts[i];
            MetricLabels labels = entry.labels;
            labels.kv["le"] = std::to_string(buckets[i]);
            oss << name << "_bucket" << labels.ToPrometheusLabelText() << " " << cumulative << "\n";
        }
        {
            MetricLabels labels = entry.labels;
            labels.kv["le"] = "+Inf";
            oss << name << "_bucket" << labels.ToPrometheusLabelText() << " " << count << "\n";
        }
        oss << name << "_sum" << entry.labels.ToPrometheusLabelText() << " " << sum << "\n";
        oss << name << "_count" << entry.labels.ToPrometheusLabelText() << " " << count << "\n";
    }

    return oss.str();
}

MetricsRegistry& DefaultMetrics() {
    static MetricsRegistry registry;
    return registry;
}

} // namespace chmicro
