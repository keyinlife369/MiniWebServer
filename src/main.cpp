#include<QApplication>
#include<QStyleFactory>
#include<QDir>
#include<filesystem>
#include"mainWindow.h"
#include"logger.h"


int main(int argc, char* argv[]) {
	QApplication app(argc, argv);

	app.setApplicationName("Mini Web Server");
	app.setApplicationVersion("1.0.0");
	app.setOrganizationName("MiniWebServer");

	app.setStyle(QStyleFactory::create("Fusion"));

	Logger::getInstance().setLogFile("server.log");
	Logger::getInstance().setLoglevel(LogLevel::DEBUG);

	LOG_INFO("========================================");
	LOG_INFO("Mini Web Server v{} starting...", "1.0.0");
	LOG_INFO("Platform: Windows + VS2022 + Boost.Asio + Qt6");
	LOG_INFO("========================================");

	std::filesystem::path static_dir = "static";
	if (!std::filesystem::exists(static_dir)) {
		std::filesystem::create_directories(static_dir);
		LOG_INFO("Created static directory: {}", static_dir.string());
	}

	std::filesystem::path index_file = static_dir / "index.html";
	if (!std::filesystem::exists(index_file)) {
		std::ofstream file(index_file);
		if (file.is_open()) {
            file << R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Mini Web Server</title>
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            max-width: 800px;
            margin: 0 auto;
            padding: 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            text-align: center;
        }
        .container {
            background: rgba(255, 255, 255, 0.1);
            border-radius: 10px;
            padding: 30px;
            backdrop-filter: blur(10px);
            margin-top: 50px;
        }
        h1 { font-size: 2.5em; }
        .features { text-align: left; display: inline-block; }
    </style>
</head>
<body>
    <div class="container">
        <h1> Mini Web Server 运行中!</h1>
        <p>基于 C++20 + Boost.Asio + Qt6</p>
        <div class="features">
            <h3>特性:</h3>
            <ul>
                <li> 异步 I/O (Boost.Asio)</li>
                <li> 线程池处理请求</li>
                <li> HTTP/1.1 GET/HEAD 支持</li>
                <li> 实时监控面板</li>
            </ul>
        </div>
    </div>
</body>
</html>)";
            file.close();
            LOG_INFO("Created default index.html");
		}

	}

    MainWindow mainWindow;
    mainWindow.show();
    LOG_INFO("Application started, UI displayed");

    return app.exec();


}