#pragma once

#include<QMainWindow>
#include<QTimer>
#include<QTableWidget>
#include<QLabel>
#include<QPushButton>
#include <QSpinBox>          
#include <QLineEdit>         
#include <QTextEdit>        
#include <QProgressBar>      
#include <QGroupBox>         
#include <memory>            
#include <queue>            
#include <mutex>           


#include"server.h"

class MainWindow :public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow();

//槽函数
public slots:

	void appendLog(const QString& message, const QString& color = "#cdd6f4");

private slots:
	//按钮
	void startServer();//启动
	void stopServer();//停止
	void clearLog();//清除日志

	//定时器
	void updateStats();//更新统计数据
	void updateConnectionList();//更新列表链接

	//设置变更
	void settingsChanged();

private:
	void setupUI();//构建UI
	void setupConnections();//连接信号和槽
	void updateServerControls();//更新控件启动状态

	std::unique_ptr<WebServer> server_;//web服务器对象

	QLabel* status_label_;//服务器状态
	QLabel* uptime_label_;//运行时长
	QLabel* active_connections_label_;//活动链接数
	QLabel* total_requests_label_;//总请求数
	QLabel* requests_per_sec_label_;//每秒请求数
	QLabel* bandwidth_label_;//带宽使用
	QProgressBar* cpu_bar_;//CPU使用率

	
	QSpinBox* port_spinbox_;// 端口号
	QLineEdit* static_dir_edit_;// 静态文件目录
	QSpinBox* threads_spinbox_;// 线程数

	QPushButton* start_button_;
	QPushButton* stop_button_;
	QPushButton* clear_log_button_;

	QTableWidget* connections_table_;//活动链接表格

	QTextEdit* log_text_;//日志文本框

	QTimer* stats_timer_;//统计刷新定时器
	QTimer* connections_timer_;//链接列表刷新定时器

};

QString formatNumber(uint64_t number);
QString formatBytes(uint64_t bytes);