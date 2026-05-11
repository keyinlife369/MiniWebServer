#include"mainWindow.h"
#include"server_monitor.h"
#include"logger.h"

#include<QVBoxLayout>
#include<QHBoxLayout>
#include<QGridLayout>
#include<QSplitter>

#include<QHeaderView>
#include<QScrollBar>
#include<QFileDialog>
#include<QMessageBox>
#include<QDateTime>
#include<QApplication>
#include<QScreen>

#include<sstream>
#include<filesystem>

MainWindow::MainWindow(QWidget* parent)
	:QMainWindow(parent)
	,stats_timer_(new QTimer(this))
	,connections_timer_(new QTimer(this)){

	setupUI();

	setupConnections();

	setWindowTitle("Mini Web Server");
	resize(1280, 800);

	QScreen* screen = QApplication::primaryScreen();
	QRect screen_geometry = screen->geometry();
	int x = (screen_geometry.width() - width()) / 2;
	int y = (screen_geometry.height() - height()) / 2;
	move(x, y);

	setStyleSheet(R"(
  QMainWindow {
            background-color: #1e1e2e;
        }
        
        /* 分组框样式 */
        QGroupBox {
            color: #cdd6f4;
            border: 2px solid #313244;
            border-radius: 8px;
            margin-top: 12px;
            font-size: 14px;
            font-weight: bold;
            padding: 16px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 8px;
        }
        
        /* 标签样式 */
        QLabel {
            color: #cdd6f4;
            font-size: 12px;
        }
        
        /* 按钮默认样式 */
        QPushButton {
            background-color: #89b4fa;
            color: #1e1e2e;
            border: none;
            border-radius: 6px;
            padding: 8px 16px;
            font-weight: bold;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #b4befe;
        }
        QPushButton:pressed {
            background-color: #74c7ec;
        }
        QPushButton:disabled {
            background-color: #45475a;
            color: #6c7086;
        }
        
        /* 输入框样式 */
        QSpinBox, QLineEdit {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 4px;
            padding: 4px 8px;
            font-size: 12px;
        }
        
        /* 表格样式 */
        QTableWidget {
            background-color: #181825;
            color: #cdd6f4;
            border: 1px solid #313244;
            gridline-color: #313244;
            font-size: 12px;
        }
        QTableWidget::item {
            padding: 4px;
        }
        QTableWidget::item:selected {
            background-color: #45475a;
        }
        
        /* 表头样式 */
        QHeaderView::section {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            padding: 6px;
            font-weight: bold;
        }
        
        /* 文本框样式 */
        QTextEdit {
            background-color: #181825;
            color: #cdd6f4;
            border: 1px solid #313244;
            font-family: 'Cascadia Code', 'Fira Code', 'Consolas', monospace;
            font-size: 11px;
        }
        
        /* 进度条样式 */
        QProgressBar {
            background-color: #313244;
            border: none;
            border-radius: 4px;
            text-align: center;
            color: #cdd6f4;
            height: 12px;
        }
        QProgressBar::chunk {
            background-color: #a6e3a1;
            border-radius: 4px;
        }
        
        /* 滚动条样式 */
        QScrollBar:vertical {
            background-color: #1e1e2e;
            width: 10px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background-color: #45475a;
            border-radius: 5px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background-color: #585b70;
        }
        
        /* 分隔条样式 */
        QSplitter::handle {
            background-color: #313244;
            height: 2px;
        })");
}

MainWindow::~MainWindow()
{
    if (server_ && server_->isRunning()) {
        server_->stopServer();
    }
}

void MainWindow::setupUI()
{
    QWidget* center_widget = new QWidget(this);
    setCentralWidget(center_widget);

    QVBoxLayout* main_layout = new QVBoxLayout(center_widget);
    main_layout->setSpacing(8);
    main_layout->setContentsMargins(16, 16, 16, 16);

    QGroupBox* status_group = new QGroupBox("服务器状态");
    QHBoxLayout* status_layout = new QHBoxLayout();

    QWidget* status_widget = new QWidget();
    QHBoxLayout* status_indicator_layout = new QHBoxLayout(status_widget);

    status_label_ = new QLabel("·已停止");
    status_label_->setStyleSheet("color: #f38ba8; font-size: 16px; font-weight: bold;");
    status_indicator_layout->addWidget(status_label_);

    uptime_label_ = new QLabel("运行时间：--:--:--");
    uptime_label_->setStyleSheet("color: #a6adc8; font-size: 13px;");
    status_indicator_layout->addWidget(uptime_label_);
    status_indicator_layout->addStretch();

    status_layout->addWidget(status_widget);
    status_group->setLayout(status_layout);
    main_layout->addWidget(status_group);

    QGroupBox* stats_group = new QGroupBox("实时统计");
    QHBoxLayout* stats_layout = new QHBoxLayout();

    auto createStatCard = [](const QString& title,QLabel*& value_label)->QWidget* {
        QWidget* card = new QWidget();
        QVBoxLayout* card_layout = new QVBoxLayout(card);
        card_layout->setSpacing(4);
        card_layout->setContentsMargins(8, 8, 8, 8);

        QLabel* title_label = new QLabel(title);
        title_label->setStyleSheet("color: #6c7086; font-size: 11px;");

        value_label = new QLabel("0");
        value_label->setStyleSheet("color: #cdd6f4; font-size: 24px; font-weight: bold;");

        card_layout->addWidget(title_label);
        card_layout->addWidget(value_label);
        card_layout->addStretch();

        return card;
    };

    stats_layout->addWidget(createStatCard("活动连接", active_connections_label_));
    stats_layout->addWidget(createStatCard("总请求", total_requests_label_));
    stats_layout->addWidget(createStatCard("请求/秒", requests_per_sec_label_));
    stats_layout->addWidget(createStatCard("带宽", bandwidth_label_));

    stats_group->setLayout(stats_layout);
    main_layout->addWidget(stats_group);

    QSplitter* splitter = new QSplitter(Qt::Vertical);

    QGroupBox* connections_group = new QGroupBox("活动连接");
    QVBoxLayout* connections_layout = new QVBoxLayout();

    connections_table_ = new QTableWidget();
    connections_table_->setColumnCount(6);
    connections_table_->setHorizontalHeaderLabels({
        "客户端IP","端口","连接时间","最后活跃","请求数","发送字节"
    });

    // 表头设置
    connections_table_->horizontalHeader()->setStretchLastSection(true);  // 最后一列自动拉伸
    connections_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);  // 所有列均匀分布
    connections_table_->setSelectionBehavior(QAbstractItemView::SelectRows);  // 整行选择
    connections_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);   // 不可编辑
    connections_table_->verticalHeader()->setVisible(false);  // 隐藏行号
    connections_table_->setAlternatingRowColors(true);  // 交替行颜色

    connections_layout->addWidget(connections_table_);
    connections_group->setLayout(connections_layout);
    splitter->addWidget(connections_group);

    // ---- 3.2 日志区域 ----
    QGroupBox* log_group = new QGroupBox("服务器日志");
    QVBoxLayout* log_layout = new QVBoxLayout();

    log_text_ = new QTextEdit();
    log_text_->setReadOnly(true);  // 只读
    log_text_->document()->setMaximumBlockCount(1000);  // 最多保留 1000 行

    // 清空日志按钮
    clear_log_button_ = new QPushButton("清空日志");
    clear_log_button_->setFixedWidth(100);
    clear_log_button_->setFixedHeight(30);

    log_layout->addWidget(log_text_);

    QHBoxLayout* log_button_layout = new QHBoxLayout();
    log_button_layout->addStretch();  // 弹性空间，按钮靠右
    log_button_layout->addWidget(clear_log_button_);
    log_layout->addLayout(log_button_layout);

    log_group->setLayout(log_layout);
    splitter->addWidget(log_group);

    // 设置分割比例（连接列表 40%，日志 60%）
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    main_layout->addWidget(splitter, 1);  // stretch factor = 1

    QGroupBox* control_group = new QGroupBox("服务器控制");
    QHBoxLayout* control_layout = new QHBoxLayout();

    // ---- 4.1 设置区域 ----
    QWidget* settings_widget = new QWidget();
    QHBoxLayout* settings_layout = new QHBoxLayout(settings_widget);
    settings_layout->setSpacing(16);

    // 端口设置
    QWidget* port_widget = new QWidget();
    QVBoxLayout* port_layout = new QVBoxLayout(port_widget);
    QLabel* port_label = new QLabel("端口:");
    port_spinbox_ = new QSpinBox();
    port_spinbox_->setRange(1, 65535);  // 端口范围
    port_spinbox_->setValue(8080);      // 默认端口
    port_spinbox_->setFixedWidth(100);
    port_spinbox_->setToolTip("服务器监听端口 (1-65535)");  // 提示文字
    port_layout->addWidget(port_label);
    port_layout->addWidget(port_spinbox_);
    settings_layout->addWidget(port_widget);

    // 线程数设置
    QWidget* threads_widget = new QWidget();
    QVBoxLayout* threads_layout = new QVBoxLayout(threads_widget);
    QLabel* threads_label = new QLabel("线程数:");
    threads_spinbox_ = new QSpinBox();
    threads_spinbox_->setRange(1, 64);  // 线程范围
    threads_spinbox_->setValue(std::thread::hardware_concurrency());  // 默认 CPU 核心数
    threads_spinbox_->setFixedWidth(100);
    threads_spinbox_->setToolTip("工作线程数量 (1-64)");
    threads_layout->addWidget(threads_label);
    threads_layout->addWidget(threads_spinbox_);
    settings_layout->addWidget(threads_widget);

    // 静态目录设置
    QWidget* dir_widget = new QWidget();
    QVBoxLayout* dir_layout = new QVBoxLayout(dir_widget);
    QLabel* dir_label = new QLabel("静态文件目录:");

    QHBoxLayout* dir_input_layout = new QHBoxLayout();
    static_dir_edit_ = new QLineEdit("./static");
    static_dir_edit_->setFixedWidth(250);
    static_dir_edit_->setToolTip("静态文件存放的目录路径");

    QPushButton* browse_button = new QPushButton("浏览...");
    browse_button->setFixedWidth(70);
    browse_button->setFixedHeight(30);

    dir_input_layout->addWidget(static_dir_edit_);
    dir_input_layout->addWidget(browse_button);
    dir_input_layout->addStretch();

    dir_layout->addWidget(dir_label);
    dir_layout->addLayout(dir_input_layout);
    settings_layout->addWidget(dir_widget);

    settings_layout->addStretch();
    control_layout->addWidget(settings_widget);

    // ---- 4.2 启动/停止按钮 ----
    QWidget* buttons_widget = new QWidget();
    QVBoxLayout* buttons_layout = new QVBoxLayout(buttons_widget);
    buttons_layout->setSpacing(8);
    buttons_layout->setContentsMargins(0, 0, 0, 0);

    // 启动按钮（绿色）
    start_button_ = new QPushButton("▶ 启动服务器");
    start_button_->setStyleSheet(R"(
        QPushButton {
            background-color: #a6e3a1;
            color: #1e1e2e;
            font-size: 14px;
            padding: 10px 24px;
            border-radius: 6px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #94e2d5;
        }
        QPushButton:disabled {
            background-color: #45475a;
            color: #6c7086;
        }
    )");
    start_button_->setFixedSize(160, 45);

    // 停止按钮（红色）
    stop_button_ = new QPushButton("■ 停止服务器");
    stop_button_->setStyleSheet(R"(
        QPushButton {
            background-color: #f38ba8;
            color: #1e1e2e;
            font-size: 14px;
            padding: 10px 24px;
            border-radius: 6px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #eba0ac;
        }
        QPushButton:disabled {
            background-color: #45475a;
            color: #6c7086;
        }
    )");
    stop_button_->setFixedSize(160, 45);
    stop_button_->setEnabled(false);  // 初始禁用

    buttons_layout->addWidget(start_button_);
    buttons_layout->addWidget(stop_button_);
    buttons_layout->addStretch();

    control_layout->addWidget(buttons_widget);

    control_group->setLayout(control_layout);
    main_layout->addWidget(control_group);

    // 浏览按钮功能
    connect(browse_button, &QPushButton::clicked, [this]() {
        QString dir = QFileDialog::getExistingDirectory(
            this,
            "选择静态文件目录",
            static_dir_edit_->text(),
            QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
        );
        if (!dir.isEmpty()) {
            static_dir_edit_->setText(dir);
        }
        });

    // 初始日志
    appendLog("欢迎使用 Mini Web Server!", "#89b4fa");
    appendLog("请配置参数后点击「启动服务器」按钮", "#a6adc8");
}

void MainWindow::setupConnections()
{
    connect(start_button_, &QPushButton::clicked, this, &MainWindow::startServer);
    connect(stop_button_, &QPushButton::clicked, this, &MainWindow::stopServer);
    connect(clear_log_button_, &QPushButton::clicked, this, &MainWindow::clearLog);

    // 设置变更信号连接
    connect(port_spinbox_, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &MainWindow::settingsChanged);
    connect(threads_spinbox_, QOverload<int>::of(&QSpinBox::valueChanged),
        this, &MainWindow::settingsChanged);
    connect(static_dir_edit_, &QLineEdit::textChanged,
        this, &MainWindow::settingsChanged);

    // 定时器信号连接
    connect(stats_timer_, &QTimer::timeout, this, &MainWindow::updateStats);
    connect(connections_timer_, &QTimer::timeout, this, &MainWindow::updateConnectionList);

    // 监听监控信号（从 ServerMonitor）
    auto& monitor = ServerMonitor::getInstance();
    connect(&monitor, &ServerMonitor::statsUpdated, this, [this]() {
        // 使用 QMetaObject::invokeMethod 确保在主线程更新 UI
        QMetaObject::invokeMethod(this, "updateStats", Qt::QueuedConnection);
        });

}

void MainWindow::startServer()
{
    if (server_ && server_->isRunning()) {
        appendLog("服务器已在运行中!", "#f9e2af");
        return;
    }

    // 获取配置参数
    int port = port_spinbox_->value();
    std::string static_dir = static_dir_edit_->text().toStdString();
    int threads = threads_spinbox_->value();

    // 确保静态目录存在
    std::error_code ec;
    if (!std::filesystem::exists(static_dir, ec)) {
        if (!std::filesystem::create_directories(static_dir, ec)) {
            appendLog("无法创建静态文件目录: " + QString::fromStdString(static_dir), "#f38ba8");
            QMessageBox::critical(this, "错误",
                QString("无法创建静态文件目录:\n%1").arg(QString::fromStdString(static_dir)));
            return;
        }
        appendLog("已创建静态文件目录: " + QString::fromStdString(static_dir), "#a6e3a1");
    }

    // 创建服务器实例
    server_ = std::make_unique<WebServer>(port, static_dir, threads);

    // 启动服务器
    if (server_->startServer()) {
        // 更新 UI 状态
        updateServerControls();

        // 启动刷新定时器
        stats_timer_->start(1000);   // 每 1 秒刷新统计
        connections_timer_->start(2000);  // 每 2 秒刷新连接列表

        // 显示启动日志
        appendLog(QString("✅ 服务器已启动在端口 %1 (%2 个工作线程)")
            .arg(port).arg(threads), "#a6e3a1");
        appendLog(QString("📂 静态文件目录: %1")
            .arg(QString::fromStdString(static_dir)), "#89b4fa");
        appendLog(QString("🌐 访问地址: http://localhost:%1")
            .arg(port), "#89b4fa");

        // 更新状态指示器
        status_label_->setText("● 运行中");
        status_label_->setStyleSheet("color: #a6e3a1; font-size: 16px; font-weight: bold;");

    }
    else {
        // 启动失败
        appendLog("❌ 服务器启动失败!", "#f38ba8");
        QMessageBox::critical(this, "启动失败",
            "服务器启动失败。\n\n"
            "可能的原因:\n"
            "• 端口已被其他程序占用\n"
            "• 权限不足 (尝试使用 >1024 的端口)\n"
            "• 静态文件目录不可访问\n\n"
            "请查看日志获取详细错误信息。");
    }
}

void MainWindow::stopServer() {
    if (!server_ || !server_->isRunning()) {
        return;
    }

    // 停止服务器
    server_->stopServer();
    server_.reset();  // 释放服务器对象

    // 停止定时器
    stats_timer_->stop();
    connections_timer_->stop();

    // 更新 UI 状态
    updateServerControls();

    // 显示停止日志
    appendLog("⏹ 服务器已停止", "#f38ba8");

    // 更新状态指示器
    status_label_->setText("● 已停止");
    status_label_->setStyleSheet("color: #f38ba8; font-size: 16px; font-weight: bold;");

    // 重置统计显示
    active_connections_label_->setText("0");
    total_requests_label_->setText("0");
    requests_per_sec_label_->setText("0");
    bandwidth_label_->setText("0 B/s");
    uptime_label_->setText("运行时间: --:--:--");
}

void MainWindow::updateStats() {
    auto& monitor = ServerMonitor::getInstance();
    auto stats = monitor.getStats();

    // 计算并格式化运行时长
    auto uptime_secs = std::chrono::duration_cast<std::chrono::seconds>(stats.uptime).count();
    int hours = uptime_secs / 3600;
    int minutes = (uptime_secs % 3600) / 60;
    int seconds = uptime_secs % 60;

    uptime_label_->setText(QString("运行时间: %1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0')));

    // 更新统计数字
    active_connections_label_->setText(QString::number(stats.active_connections));
    total_requests_label_->setText(formatNumber(stats.total_requests));
    requests_per_sec_label_->setText(QString::number(stats.requests_per_second, 'f', 1));

    // 格式化带宽显示
    QString bandwidth;
    double bytes_per_sec = stats.bytes_per_second;
    if (bytes_per_sec >= 1024.0 * 1024.0) {
        bandwidth = QString::number(bytes_per_sec / (1024.0 * 1024.0), 'f', 2) + " MB/s";
    }
    else if (bytes_per_sec >= 1024.0) {
        bandwidth = QString::number(bytes_per_sec / 1024.0, 'f', 1) + " KB/s";
    }
    else {
        bandwidth = QString::number(bytes_per_sec, 'f', 0) + " B/s";
    }
    bandwidth_label_->setText(bandwidth);
}

void MainWindow::updateConnectionList() {
    auto& monitor = ServerMonitor::getInstance();
    auto connections = monitor.getActiveConnections();

    // 设置表格行数
    connections_table_->setRowCount(static_cast<int>(connections.size()));

    // 填充每一行数据
    for (int i = 0; i < static_cast<int>(connections.size()); ++i) {
        const auto& conn = connections[i];

        // 格式化最后活跃时间
        auto now = std::chrono::steady_clock::now();
        auto last_active_secs = std::chrono::duration_cast<std::chrono::seconds>(
            now - conn.last_activity).count();

        QString last_active_text;
        if (last_active_secs < 60) {
            last_active_text = QString("%1 秒前").arg(last_active_secs);
        }
        else if (last_active_secs < 3600) {
            last_active_text = QString("%1 分钟前").arg(last_active_secs / 60);
        }
        else {
            last_active_text = QString("%1 小时前").arg(last_active_secs / 3600);
        }

        // 设置表格单元格
        connections_table_->setItem(i, 0, new QTableWidgetItem(conn.q_client_ip));
        connections_table_->setItem(i, 1, new QTableWidgetItem(QString::number(conn.client_port)));
        connections_table_->setItem(i, 2, new QTableWidgetItem(conn.q_connected_time));
        connections_table_->setItem(i, 3, new QTableWidgetItem(last_active_text));
        connections_table_->setItem(i, 4, new QTableWidgetItem(QString::number(conn.q_requests_handled)));
        connections_table_->setItem(i, 5, new QTableWidgetItem(formatBytes(conn.q_bytes_sent)));
    }

    // 如果连接数为 0，显示提示
    if (connections.empty()) {
        connections_table_->setRowCount(1);
        connections_table_->setItem(0, 0, new QTableWidgetItem("暂无活动连接"));
        connections_table_->setSpan(0, 0, 1, 6);  // 合并第 1 行的所有列
        connections_table_->item(0, 0)->setTextAlignment(Qt::AlignCenter);
    }

}

void MainWindow::clearLog() {
    log_text_->clear();
    appendLog("日志已清空", "#6c7086");
}

void MainWindow::settingsChanged() {
    if (server_ && server_->isRunning()) {
        appendLog("⚠ 设置已更改，需要重启服务器才能生效", "#f9e2af");
    }
}

void MainWindow::updateServerControls() {
    bool running = server_ && server_->isRunning();

    // 运行中：禁用启动按钮，启用停止按钮
    start_button_->setEnabled(!running);
    stop_button_->setEnabled(running);

    // 运行中：禁用设置控件（防止运行时修改）
    port_spinbox_->setEnabled(!running);
    threads_spinbox_->setEnabled(!running);
    static_dir_edit_->setEnabled(!running);
}

void MainWindow::appendLog(const QString& message, const QString& color ) {
    // 获取当前时间戳
    QDateTime now = QDateTime::currentDateTime();
    QString timestamp = now.toString("HH:mm:ss.zzz");

    // 构建 HTML 格式的日志行
    // 使用 <span> 标签实现不同颜色
    QString formatted_message = QString(
        "<span style='color: #6c7086;'>[%1]</span> "
        "<span style='color: %2;'>%3</span>"
    ).arg(timestamp, color, message.toHtmlEscaped());

    // 添加到日志文本框
    log_text_->append(formatted_message);

    // 自动滚动到底部
    QScrollBar* scrollbar = log_text_->verticalScrollBar();
    if (scrollbar) {
        scrollbar->setValue(scrollbar->maximum());
    }
}

QString formatNumber(uint64_t number) {
    if (number >= 1000000) {
        return QString::number(number / 1000000.0, 'f', 1) + "M";
    }
    else if (number >= 1000) {
        return QString::number(number / 1000.0, 'f', 1) + "K";
    }
    return QString::number(number);
}

QString formatBytes(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
    }
    else if (bytes >= 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024), 'f', 2) + " MB";
    }
    else if (bytes >= 1024) {
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    }
    return QString::number(bytes) + " B";
}
