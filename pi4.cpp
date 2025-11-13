/*
 * STM32 Oscilloscope - Debug Version (FIXED)
 * Compile: g++ -o scope pi4.cpp -std=c++17 $(pkg-config --cflags --libs Qt5Widgets) -fPIC
 */

#include <QApplication>
#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <QTextEdit>
#include <cstdint>
#include <vector>
#include <array>
#include <atomic>
#include <thread>
#include <mutex>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

constexpr uint8_t  MARKER_START = 0xAA;
constexpr uint8_t  MARKER_HEADER = 0x55;
constexpr uint16_t BUFFER_SIZE = 256;
constexpr uint16_t PACKET_SIZE = 4 + BUFFER_SIZE * 2;
constexpr float    SAMPLE_RATE = 250000.0f;
constexpr float    VCC = 3.3f;
constexpr uint16_t ADC_MAX = 4095;
constexpr size_t   PRETRIGGER_SIZE = 128;
constexpr size_t   CAPTURE_SIZE = 3000;

// ============================================================================
// Debug Logger
// ============================================================================
class DebugLogger {
private:
    std::mutex log_mutex;
    std::vector<std::string> log_buffer;
    const size_t max_lines = 100;

public:
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(log_mutex);

        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ) % 1000;

        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time_t);

        std::ostringstream oss;
        oss << std::setfill('0')
            << std::setw(2) << tm.tm_hour << ":"
            << std::setw(2) << tm.tm_min << ":"
            << std::setw(2) << tm.tm_sec << "."
            << std::setw(3) << ms.count() << " | " << msg;

        log_buffer.push_back(oss.str());
        if (log_buffer.size() > max_lines) {
            log_buffer.erase(log_buffer.begin());
        }

        std::cout << oss.str() << std::endl;
    }

    std::vector<std::string> get_logs() {
        std::lock_guard<std::mutex> lock(log_mutex);
        return log_buffer;
    }
};

// ============================================================================
// Timing Statistics (NON-COPYABLE - use load to get values)
// ============================================================================
struct TimingStats {
    std::atomic<uint64_t> total_packets{0};
    std::atomic<uint64_t> good_packets{0};
    std::atomic<uint64_t> marker_fails{0};
    std::atomic<uint64_t> adc_fails{0};

    std::atomic<double> min_interval_ms{999999.0};
    std::atomic<double> max_interval_ms{0.0};
    std::atomic<double> avg_interval_ms{0.0};

    std::atomic<uint64_t> gaps_detected{0};
    std::atomic<double> last_gap_ms{0.0};

    void reset() {
        total_packets = 0;
        good_packets = 0;
        marker_fails = 0;
        adc_fails = 0;
        min_interval_ms = 999999.0;
        max_interval_ms = 0.0;
        avg_interval_ms = 0.0;
        gaps_detected = 0;
        last_gap_ms = 0.0;
    }
};

// Snapshot for copying stats
struct TimingSnapshot {
    uint64_t total_packets;
    uint64_t good_packets;
    uint64_t marker_fails;
    uint64_t adc_fails;
    double min_interval_ms;
    double max_interval_ms;
    double avg_interval_ms;
    uint64_t gaps_detected;
    double last_gap_ms;
};

// ============================================================================
// SPI Reader with Debug
// ============================================================================
class SPIReader {
private:
    int spi_fd = -1;
    std::atomic<bool> running{false};
    std::thread reader_thread;
    std::mutex data_mutex;

    std::array<float, 8192> display_voltage;
    std::array<float, 8192> display_time;
    std::atomic<size_t> display_size{0};

    std::atomic<bool> new_data{false};
    std::atomic<float> fps{0.0f};
    std::atomic<bool> trigger_enabled{true};

    // Trigger state
    enum class State { IDLE, COLLECTING, HOLDOFF };
    State state = State::IDLE;
    size_t collect_count = 0;
    int holdoff_count = 0;
    std::array<float, 8192> temp_buffer;
    size_t temp_size = 0;

    // Pre-trigger circular buffer
    std::array<float, 1024> pretrigger_buffer;
    size_t pretrigger_write_idx = 0;
    bool pretrigger_ready = false;

    // Timing tracking
    std::chrono::steady_clock::time_point last_packet_time;
    std::chrono::steady_clock::time_point first_packet_time;
    bool first_packet_received = false;

    // Debug & Stats
    DebugLogger logger;
    TimingStats stats;

public:
    SPIReader() {
        display_voltage.fill(0);
        display_time.fill(0);
        temp_buffer.fill(0);
        pretrigger_buffer.fill(0);
    }

    ~SPIReader() { stop(); }

    bool init() {
        logger.log("🔧 Initializing SPI...");

        spi_fd = open("/dev/spidev0.0", O_RDWR);
        if (spi_fd < 0) {
            logger.log("❌ FATAL: Cannot open /dev/spidev0.0");
            std::cerr << "Error: " << strerror(errno) << std::endl;
            return false;
        }

        uint8_t mode = SPI_MODE_0;
        uint8_t bits = 8;
        uint32_t speed = 8000000;

        if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
            logger.log("❌ Failed to set SPI mode");
            close(spi_fd);
            return false;
        }

        if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
            logger.log("❌ Failed to set SPI bits per word");
            close(spi_fd);
            return false;
        }

        if (ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
            logger.log("❌ Failed to set SPI speed");
            close(spi_fd);
            return false;
        }

        logger.log("✅ SPI initialized: 8MHz, Mode 0, 8 bits");
        return true;
    }

    void start() {
        logger.log("▶️  Starting SPI reader thread...");
        running = true;
        stats.reset();
        first_packet_received = false;
        reader_thread = std::thread(&SPIReader::reader_loop, this);
    }

    void stop() {
        logger.log("⏹️  Stopping SPI reader...");
        running = false;
        if (reader_thread.joinable()) reader_thread.join();
        if (spi_fd >= 0) { close(spi_fd); spi_fd = -1; }
        logger.log("✅ SPI reader stopped");
    }

    bool get_data(std::vector<float>& volt, std::vector<float>& time) {
        if (!new_data.load()) return false;

        std::lock_guard<std::mutex> lock(data_mutex);
        size_t n = display_size.load();
        if (n == 0) return false;

        volt.clear();
        time.clear();
        volt.reserve(n);
        time.reserve(n);

        for (size_t i = 0; i < n; i++) {
            volt.push_back(display_voltage[i]);
            time.push_back(display_time[i]);
        }

        new_data = false;
        return true;
    }

    // FIXED: Return snapshot instead of copying atomics
    TimingSnapshot get_stats() const {
        TimingSnapshot snap;
        snap.total_packets = stats.total_packets.load();
        snap.good_packets = stats.good_packets.load();
        snap.marker_fails = stats.marker_fails.load();
        snap.adc_fails = stats.adc_fails.load();
        snap.min_interval_ms = stats.min_interval_ms.load();
        snap.max_interval_ms = stats.max_interval_ms.load();
        snap.avg_interval_ms = stats.avg_interval_ms.load();
        snap.gaps_detected = stats.gaps_detected.load();
        snap.last_gap_ms = stats.last_gap_ms.load();
        return snap;
    }

    std::vector<std::string> get_logs() {
        return logger.get_logs();
    }

    float get_fps() const { return fps.load(); }

    void set_trigger(bool enabled) {
        trigger_enabled = enabled;
        state = State::IDLE;
        collect_count = 0;
        holdoff_count = 0;
        temp_size = 0;
        pretrigger_write_idx = 0;
        pretrigger_ready = false;
        logger.log(enabled ? "🎯 Trigger ENABLED" : "🔄 Trigger DISABLED (Free run)");
    }

private:
    void reader_loop() {
        uint32_t frame_count = 0;
        auto last_fps_time = std::chrono::steady_clock::now();

        std::vector<uint8_t> rx_buf(PACKET_SIZE);
        std::vector<uint8_t> tx_buf(PACKET_SIZE, 0x00);

        logger.log("🔁 Reader loop started");

        while (running) {
            struct spi_ioc_transfer tr{};
            tr.tx_buf = reinterpret_cast<uint64_t>(tx_buf.data());
            tr.rx_buf = reinterpret_cast<uint64_t>(rx_buf.data());
            tr.len = PACKET_SIZE;
            tr.speed_hz = 8000000;
            tr.bits_per_word = 8;

            if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
                logger.log("⚠️  SPI transfer failed");
                continue;
            }

            stats.total_packets++;

            // Calculate timing
            auto now = std::chrono::steady_clock::now();
            if (first_packet_received) {
                double interval_ms = std::chrono::duration<double, std::milli>(
                    now - last_packet_time
                ).count();

                // Update min/max
                double current_min = stats.min_interval_ms.load();
                while (interval_ms < current_min &&
                       !stats.min_interval_ms.compare_exchange_weak(current_min, interval_ms));

                double current_max = stats.max_interval_ms.load();
                while (interval_ms > current_max &&
                       !stats.max_interval_ms.compare_exchange_weak(current_max, interval_ms));

                // Update average
                double current_avg = stats.avg_interval_ms.load();
                double new_avg = current_avg * 0.95 + interval_ms * 0.05;
                stats.avg_interval_ms = new_avg;

                // Detect gaps (> 10ms)
                if (interval_ms > 10.0) {
                    stats.gaps_detected++;
                    stats.last_gap_ms = interval_ms;

                    std::ostringstream oss;
                    oss << "⚠️  GAP: " << std::fixed << std::setprecision(2)
                        << interval_ms << "ms between packets!";
                    logger.log(oss.str());
                }
            } else {
                first_packet_received = true;
                first_packet_time = now;
                logger.log("📦 First packet received!");
            }
            last_packet_time = now;

            // Parse packet
            if (parse_packet(rx_buf)) {
                stats.good_packets++;
                frame_count++;
            }

            // Calculate FPS
            auto fps_elapsed = std::chrono::duration<float>(now - last_fps_time).count();
            if (fps_elapsed >= 1.0f) {
                fps = frame_count / fps_elapsed;
                frame_count = 0;
                last_fps_time = now;

                // Periodic stats log
                if (stats.total_packets % 1000 == 0) {
                    std::ostringstream oss;
                    oss << "📊 Stats: Good=" << stats.good_packets.load()
                        << " Total=" << stats.total_packets.load()
                        << " Success=" << std::fixed << std::setprecision(1)
                        << (100.0 * stats.good_packets.load() / stats.total_packets.load()) << "%"
                        << " AvgInterval=" << std::setprecision(2)
                        << stats.avg_interval_ms.load() << "ms";
                    logger.log(oss.str());
                }
            }
        }

        logger.log("🔁 Reader loop exited");
    }

    bool parse_packet(const std::vector<uint8_t>& buf) {
        // Find marker
        int marker_pos = -1;
        for (size_t i = 0; i <= buf.size() - PACKET_SIZE; i++) {
            if (buf[i] == MARKER_START && buf[i+1] == MARKER_HEADER) {
                marker_pos = i;
                break;
            }
        }

        if (marker_pos < 0) {
            stats.marker_fails++;
            if (stats.marker_fails.load() % 100 == 1) {
                logger.log("⚠️  Marker not found");
            }
            return false;
        }

        // Parse frame counter
        uint16_t frame_num = (static_cast<uint16_t>(buf[marker_pos + 2]) << 8) |
                             buf[marker_pos + 3];

        // Parse ADC samples
        std::array<uint16_t, BUFFER_SIZE> samples;
        bool adc_error = false;
        for (size_t i = 0; i < BUFFER_SIZE; i++) {
            size_t offset = marker_pos + 4 + i * 2;
            uint16_t val = (static_cast<uint16_t>(buf[offset]) << 8) | buf[offset + 1];
            if (val > ADC_MAX) {
                adc_error = true;
                break;
            }
            samples[i] = val;
        }

        if (adc_error) {
            stats.adc_fails++;
            return false;
        }

        // Convert to voltage
        std::array<float, BUFFER_SIZE> voltage;
        for (size_t i = 0; i < BUFFER_SIZE; i++) {
            voltage[i] = samples[i] * (VCC / ADC_MAX);
        }

        // Log first few packets
        static int log_count = 0;
        if (log_count < 3) {
            std::ostringstream oss;
            oss << "📦 Frame #" << frame_num
                << " | V=" << std::fixed << std::setprecision(3)
                << voltage[0] << "V, " << voltage[1] << "V, "
                << voltage[2] << "V, " << voltage[3] << "V";
            logger.log(oss.str());
            log_count++;
        }

        if (!trigger_enabled.load()) {
            // FREE RUN
            std::lock_guard<std::mutex> lock(data_mutex);
            for (size_t i = 0; i < BUFFER_SIZE; i++) {
                display_voltage[i] = voltage[i];
                display_time[i] = static_cast<float>(i) / SAMPLE_RATE;
            }
            display_size = BUFFER_SIZE;
            new_data = true;
            return true;
        }

        return process_trigger(voltage);
    }

    bool process_trigger(const std::array<float, BUFFER_SIZE>& voltage) {
        switch (state) {
            case State::HOLDOFF:
                holdoff_count--;
                if (holdoff_count <= 0) state = State::IDLE;
                return false;

            case State::IDLE: {
                // Store pre-trigger
                for (size_t i = 0; i < BUFFER_SIZE; i++) {
                    pretrigger_buffer[pretrigger_write_idx] = voltage[i];
                    pretrigger_write_idx = (pretrigger_write_idx + 1) % pretrigger_buffer.size();
                }
                if (pretrigger_write_idx == 0) pretrigger_ready = true;

                // Find trigger
                float trigger_level = VCC * 0.50f;
                float hysteresis = VCC * 0.15f;
                int trigger_idx = -1;

                for (size_t i = 50; i < BUFFER_SIZE - 50; i++) {
                    bool stable_low = true;
                    bool stable_high = true;

                    for (int j = -5; j < 0; j++) {
                        if (voltage[i + j] >= (trigger_level - hysteresis)) {
                            stable_low = false;
                            break;
                        }
                    }

                    for (int j = 0; j < 5; j++) {
                        if (voltage[i + j] <= (trigger_level + hysteresis)) {
                            stable_high = false;
                            break;
                        }
                    }

                    if (stable_low && stable_high) {
                        float slope = voltage[i] - voltage[i-1];
                        if (slope > VCC * 0.1f) {
                            trigger_idx = i;
                            break;
                        }
                    }
                }

                if (trigger_idx < 0) return false;

                logger.log("🎯 Trigger! Starting capture...");

                state = State::COLLECTING;
                collect_count = 0;
                temp_size = 0;

                // Add pre-trigger
                if (pretrigger_ready) {
                    size_t start_idx = (pretrigger_write_idx + pretrigger_buffer.size() - PRETRIGGER_SIZE)
                                       % pretrigger_buffer.size();
                    for (size_t i = 0; i < PRETRIGGER_SIZE; i++) {
                        temp_buffer[temp_size++] = pretrigger_buffer[(start_idx + i) % pretrigger_buffer.size()];
                    }
                }

                // Add post-trigger
                for (size_t i = trigger_idx; i < BUFFER_SIZE && temp_size < temp_buffer.size(); i++) {
                    temp_buffer[temp_size++] = voltage[i];
                }

                collect_count++;
                break;
            }

            case State::COLLECTING: {
                for (size_t i = 0; i < BUFFER_SIZE && temp_size < CAPTURE_SIZE; i++) {
                    temp_buffer[temp_size++] = voltage[i];
                }
                collect_count++;

                if (temp_size >= CAPTURE_SIZE) {
                    std::lock_guard<std::mutex> lock(data_mutex);

                    size_t copy_size = std::min(temp_size, CAPTURE_SIZE);
                    for (size_t i = 0; i < copy_size; i++) {
                        display_voltage[i] = temp_buffer[i];
                        display_time[i] = (static_cast<float>(i) - PRETRIGGER_SIZE) / SAMPLE_RATE;
                    }
                    display_size = copy_size;
                    new_data = true;

                    logger.log("✅ Capture complete! " + std::to_string(copy_size) + " samples");

                    state = State::HOLDOFF;
                    holdoff_count = 60;
                }
                break;
            }
        }

        return true;
    }
};

// ============================================================================
// Display Widget
// ============================================================================
class ScopeDisplay : public QWidget {
    Q_OBJECT

private:
    std::vector<float> voltage;
    std::vector<float> time;
    float time_div = 0.001f;
    float volt_div = 0.5f;
    bool trigger_enabled = true;

public:
    ScopeDisplay(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(800, 600);
    }

    void update_waveform(const std::vector<float>& v, const std::vector<float>& t) {
        voltage = v;
        time = t;
        update();
    }

    void set_time_div(float div) { time_div = div; update(); }
    void set_volt_div(float div) { volt_div = div; update(); }
    void set_trigger(bool enabled) { trigger_enabled = enabled; update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int w = width();
        int h = height();
        int margin = 50;
        int grid_w = w - 2 * margin;
        int grid_h = h - 2 * margin;

        p.fillRect(0, 0, w, h, QColor(15, 15, 15));

        // Grid
        p.setPen(QPen(QColor(0, 100, 0), 1));
        for (int i = 0; i <= 10; i++) {
            int x = margin + i * grid_w / 10;
            p.drawLine(x, margin, x, margin + grid_h);
        }
        for (int i = 0; i <= 8; i++) {
            int y = margin + i * grid_h / 8;
            p.drawLine(margin, y, margin + grid_w, y);
        }

        // Center
        p.setPen(QPen(QColor(0, 180, 0), 2));
        int cx = margin + grid_w / 2;
        int cy = margin + grid_h / 2;
        p.drawLine(cx, margin, cx, margin + grid_h);
        p.drawLine(margin, cy, margin + grid_w, cy);

        // Trigger
        if (trigger_enabled) {
            p.setPen(QPen(QColor(255, 140, 0), 3));
            int trigger_x = margin + grid_w / 10;
            p.drawLine(trigger_x, margin, trigger_x, margin + grid_h);
            p.setPen(QPen(QColor(255, 140, 0), 2, Qt::DashLine));
            p.drawLine(margin, cy, margin + grid_w, cy);
        }

        // Waveform
        if (!voltage.empty()) {
            p.setPen(QPen(QColor(255, 220, 0), 2));

            float t_window = time_div * 10.0f;
            float v_center = VCC / 2.0f;
            float v_range = volt_div * 8.0f;
            float t_offset = time_div * 1.0f;

            for (size_t i = 0; i < voltage.size() - 1; i++) {
                float t1 = time[i] + t_offset;
                float t2 = time[i + 1] + t_offset;

                if (t2 < 0 || t1 > t_window) continue;

                int x1 = margin + static_cast<int>((t1 / t_window) * grid_w);
                int x2 = margin + static_cast<int>((t2 / t_window) * grid_w);
                int y1 = cy - static_cast<int>((voltage[i] - v_center) / v_range * grid_h);
                int y2 = cy - static_cast<int>((voltage[i+1] - v_center) / v_range * grid_h);

                p.drawLine(x1, y1, x2, y2);
            }

            // Measurements
            float vmin = *std::min_element(voltage.begin(), voltage.end());
            float vmax = *std::max_element(voltage.begin(), voltage.end());

            p.setPen(QColor(255, 220, 0));
            p.setFont(QFont("Monospace", 11, QFont::Bold));
            p.drawText(10, 30, QString("Samples: %1").arg(voltage.size()));
            p.drawText(10, 50, QString("Vpp: %1V").arg(vmax - vmin, 0, 'f', 2));
            p.drawText(10, 70, QString("Vmax: %1V").arg(vmax, 0, 'f', 2));
            p.drawText(10, 90, QString("Vmin: %1V").arg(vmin, 0, 'f', 2));
        }

        // Scale
        p.setPen(QColor(200, 200, 200));
        p.setFont(QFont("Monospace", 12, QFont::Bold));
        QString time_str = (time_div >= 0.001f)
            ? QString("%1ms/div").arg(time_div * 1000, 0, 'f', 1)
            : QString("%1µs/div").arg(time_div * 1e6, 0, 'f', 0);
        p.drawText(margin, h - 15, time_str);
        p.drawText(margin + 200, h - 15, QString("%1V/div").arg(volt_div, 0, 'f', 2));
    }
};

// ============================================================================
// Main Window
// ============================================================================
class MainWindow : public QWidget {
    Q_OBJECT

private:
    SPIReader reader;
    ScopeDisplay *display;
    QLabel *stats_label;
    QTextEdit *debug_log;
    QTimer *timer;
    QCheckBox *trigger_checkbox;

public:
    MainWindow() {
        setWindowTitle("STM32 Oscilloscope - Debug");
        resize(1400, 900);

        QVBoxLayout *main_layout = new QVBoxLayout(this);

        QHBoxLayout *top = new QHBoxLayout();
        display = new ScopeDisplay();
        top->addWidget(display, 3);

        // Control panel
        QWidget *panel = new QWidget();
        panel->setStyleSheet("background-color: #2a2a2a;");
        panel->setFixedWidth(200);

        QVBoxLayout *panel_layout = new QVBoxLayout(panel);
        panel_layout->setSpacing(10);
        panel_layout->setContentsMargins(10, 10, 10, 10);

        trigger_checkbox = new QCheckBox("TRIGGER");
        trigger_checkbox->setChecked(true);
        trigger_checkbox->setStyleSheet("color: #ff8800; font-weight: bold; font-size: 14px;");
        connect(trigger_checkbox, &QCheckBox::toggled, this, &MainWindow::toggle_trigger);
        panel_layout->addWidget(trigger_checkbox);

        panel_layout->addSpacing(15);

        QLabel *time_label = new QLabel("TIME/DIV");
        time_label->setStyleSheet("color: #00ff00; font-weight: bold;");
        panel_layout->addWidget(time_label);

        std::vector<std::pair<QString, float>> time_scales = {
            {"500µs", 0.0005f}, {"1ms", 0.001f}, {"2ms", 0.002f}, {"5ms", 0.005f}
        };

        for (const auto& [name, val] : time_scales) {
            QPushButton *btn = new QPushButton(name);
            btn->setStyleSheet("background-color: #444; color: white; padding: 6px;");
            connect(btn, &QPushButton::clicked, [this, val]() { display->set_time_div(val); });
            panel_layout->addWidget(btn);
        }

        panel_layout->addSpacing(15);

        QLabel *volt_label = new QLabel("VOLTS/DIV");
        volt_label->setStyleSheet("color: #ffff00; font-weight: bold;");
        panel_layout->addWidget(volt_label);

        std::vector<std::pair<QString, float>> volt_scales = {
            {"0.5V", 0.5f}, {"1.0V", 1.0f}, {"2.0V", 2.0f}
        };

        for (const auto& [name, val] : volt_scales) {
            QPushButton *btn = new QPushButton(name);
            btn->setStyleSheet("background-color: #444; color: white; padding: 6px;");
            connect(btn, &QPushButton::clicked, [this, val]() { display->set_volt_div(val); });
            panel_layout->addWidget(btn);
        }

        panel_layout->addStretch();

        stats_label = new QLabel();
        stats_label->setStyleSheet("color: #00ffff; font-size: 9px; font-family: monospace;");
        stats_label->setWordWrap(true);
        panel_layout->addWidget(stats_label);

        top->addWidget(panel);
        main_layout->addLayout(top, 3);

        // Debug log
        QLabel *debug_title = new QLabel("📊 DEBUG LOG:");
        debug_title->setStyleSheet("color: #ffff00; font-weight: bold; font-size: 12px; padding: 5px;");
        main_layout->addWidget(debug_title);

        debug_log = new QTextEdit();
        debug_log->setReadOnly(true);
        debug_log->setStyleSheet(
            "background-color: #1a1a1a; color: #00ff00; "
            "font-family: monospace; font-size: 10px;"
        );
        debug_log->setMaximumHeight(200);
        main_layout->addWidget(debug_log, 1);

        if (!reader.init()) {
            stats_label->setText("❌ SPI FAILED!");
            return;
        }

        reader.start();

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &MainWindow::update_display);
        timer->start(50);
    }

    ~MainWindow() {
        reader.stop();
    }

private slots:
    void update_display() {
        std::vector<float> volt, time_vec;
        if (reader.get_data(volt, time_vec)) {
            display->update_waveform(volt, time_vec);
        }

        TimingSnapshot snap = reader.get_stats();
        float fps_val = reader.get_fps();

        float success = (snap.total_packets > 0)
            ? (100.0f * snap.good_packets / snap.total_packets) : 0;

        QString stats_text = QString(
            "📊 STATS\n"
            "FPS: %1\n"
            "Total: %2\n"
            "Good: %3\n"
            "Success: %4%\n\n"
            "⏱️  TIMING\n"
            "Min: %5ms\n"
            "Max: %6ms\n"
            "Avg: %7ms\n\n"
            "⚠️  ERRORS\n"
            "Marker: %8\n"
            "ADC: %9\n"
            "Gaps: %10\n"
            "Last: %11ms"
        ).arg(fps_val, 0, 'f', 1)
         .arg(snap.total_packets)
         .arg(snap.good_packets)
         .arg(success, 0, 'f', 1)
         .arg(snap.min_interval_ms, 0, 'f', 2)
         .arg(snap.max_interval_ms, 0, 'f', 2)
         .arg(snap.avg_interval_ms, 0, 'f', 2)
         .arg(snap.marker_fails)
         .arg(snap.adc_fails)
         .arg(snap.gaps_detected)
         .arg(snap.last_gap_ms, 0, 'f', 2);

        stats_label->setText(stats_text);

        auto logs = reader.get_logs();
        QString log_text;
        for (const auto& line : logs) {
            log_text += QString::fromStdString(line) + "\n";
        }
        debug_log->setPlainText(log_text);
        debug_log->moveCursor(QTextCursor::End);
    }

    void toggle_trigger(bool checked) {
        reader.set_trigger(checked);
        display->set_trigger(checked);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}

#include "main.moc"
