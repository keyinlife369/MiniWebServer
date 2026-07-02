#include<QApplication>
#include<QStyleFactory>
#include<QDir>
#include<filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

#include"mainWindow.h"
#include"logger.h"


int main(int argc, char* argv[]) {

#ifdef _WIN32
    // ========== 设置 Windows 控制台为 UTF-8 ==========
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    // =================================================
#endif

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
                <li> HTTP/1.1 GET/HEAD/POST 支持</li>
                <li> URL 路由分发系统</li>
                <li> 实时监控面板</li>
            </ul>
            <p><a href="/form.html" style="color:#a6e3a1;">→ POST 测试表单</a></p>
            <p><a href="/api/info" style="color:#89b4fa;">→ 服务器状态 API</a></p>
        </div>
    </div>
</body>
</html>)";
            file.close();
            LOG_INFO("Created default index.html");
		}

	}

	// 生成 POST 测试表单页面
	std::filesystem::path form_file = static_dir / "form.html";
	if (!std::filesystem::exists(form_file)) {
		std::ofstream file(form_file);
		if (file.is_open()) {
			file << R"(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>POST 测试表单</title>
    <style>
        body {
            font-family: 'Segoe UI', sans-serif;
            max-width: 600px;
            margin: 50px auto;
            background: #1e1e2e;
            color: #cdd6f4;
        }
        h1 { color: #89b4fa; }
        form {
            background: #313244;
            padding: 24px;
            border-radius: 12px;
        }
        label {
            display: block;
            margin: 12px 0 4px;
            color: #a6adc8;
            font-size: 14px;
        }
        input, textarea {
            width: 100%;
            padding: 10px;
            border: 1px solid #45475a;
            border-radius: 6px;
            background: #181825;
            color: #cdd6f4;
            font-size: 14px;
            box-sizing: border-box;
        }
        textarea { resize: vertical; min-height: 80px; }
        button {
            margin-top: 16px;
            padding: 10px 24px;
            background: #a6e3a1;
            color: #1e1e2e;
            border: none;
            border-radius: 6px;
            font-size: 14px;
            font-weight: bold;
            cursor: pointer;
        }
        button:hover { background: #94e2d5; }
        .result {
            margin-top: 20px;
            padding: 16px;
            background: #181825;
            border-radius: 8px;
            font-family: monospace;
            font-size: 13px;
            white-space: pre-wrap;
            display: none;
        }
        .nav { margin-bottom: 16px; }
        .nav a { color: #89b4fa; text-decoration: none; }
    </style>
</head>
<body>
    <div class="nav"><a href="/">← 返回首页</a></div>
    <h1>POST 请求测试</h1>
    <form id="echoForm">
        <label>名称:</label>
        <input type="text" name="name" placeholder="输入你的名字">
        <label>消息:</label>
        <textarea name="message" rows="4" placeholder="输入你的消息"></textarea>
        <button type="submit">发送 POST 请求</button>
    </form>
    <div class="result" id="result"></div>

    <script>
        document.getElementById('echoForm').addEventListener('submit', async function(e) {
            e.preventDefault();
            const formData = new FormData(this);
            const params = new URLSearchParams(formData);
            try {
                const resp = await fetch('/api/echo', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: params.toString()
                });
                const json = await resp.json();
                const resultEl = document.getElementById('result');
                resultEl.style.display = 'block';
                resultEl.textContent = JSON.stringify(json, null, 2);
            } catch (err) {
                const resultEl = document.getElementById('result');
                resultEl.style.display = 'block';
                resultEl.textContent = '错误: ' + err.message;
            }
        });
    </script>
</body>
</html>)";
			file.close();
			LOG_INFO("Created form.html for POST testing");
		}
	}

	// 生成留言板测试页面
	std::filesystem::path guestbook_file = static_dir / "guestbook.html";
	if (!std::filesystem::exists(guestbook_file)) {
		std::ofstream file(guestbook_file);
		if (file.is_open()) {
			file << R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>留言板 - Mini Web Server</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', sans-serif;
            max-width: 700px;
            margin: 40px auto;
            background: #1e1e2e;
            color: #cdd6f4;
        }
        h1 { color: #89b4fa; margin-bottom: 8px; }
        .nav { margin-bottom: 20px; }
        .nav a { color: #89b4fa; text-decoration: none; }
        .card {
            background: #313244;
            border-radius: 12px;
            padding: 20px;
            margin-bottom: 16px;
        }
        label { display: block; margin: 10px 0 4px; color: #a6adc8; font-size: 13px; }
        input, textarea {
            width: 100%;
            padding: 10px;
            border: 1px solid #45475a;
            border-radius: 6px;
            background: #181825;
            color: #cdd6f4;
            font-size: 14px;
        }
        textarea { resize: vertical; min-height: 60px; }
        button {
            margin-top: 12px;
            padding: 10px 24px;
            background: #a6e3a1;
            color: #1e1e2e;
            border: none;
            border-radius: 6px;
            font-size: 14px;
            font-weight: bold;
            cursor: pointer;
        }
        button:hover { background: #94e2d5; }
        button.danger { background: #f38ba8; }
        button.danger:hover { background: #eba0ac; }
        .msg {
            background: #181825;
            border-radius: 8px;
            padding: 14px;
            margin-bottom: 10px;
        }
        .msg .meta {
            font-size: 12px;
            color: #6c7086;
            margin-bottom: 4px;
        }
        .msg .meta .author { color: #89b4fa; font-weight: bold; margin-right: 12px; }
        .msg .content { font-size: 14px; white-space: pre-wrap; }
        .msg .del-btn {
            float: right;
            background: none;
            color: #f38ba8;
            border: 1px solid #f38ba8;
            padding: 2px 10px;
            border-radius: 4px;
            font-size: 12px;
            margin-top: 0;
            cursor: pointer;
        }
        .msg .del-btn:hover { background: #f38ba8; color: #1e1e2e; }
        .clearfix::after { content: ""; display: table; clear: both; }
        .status { color: #a6adc8; font-size: 13px; margin-bottom: 10px; }
    </style>
</head>
<body>
    <div class="nav"><a href="/">← 返回首页</a> | <a href="/api/info">服务器状态</a></div>
    <h1>留言板</h1>
    <p style="color:#6c7086;margin-bottom:20px;">基于 MySQL 9.5 + C++ RESTful API</p>

    <div class="card">
        <h3 style="color:#a6e3a1;">发布留言</h3>
        <label>昵称:</label>
        <input type="text" id="author" placeholder="你的名字" maxlength="100">
        <label>内容:</label>
        <textarea id="content" rows="3" placeholder="说点什么..." maxlength="5000"></textarea>
        <button onclick="postMessage()">发送留言</button>
        <span id="postStatus" class="status"></span>
    </div>

    <div id="messages"></div>

    <script>
        async function loadMessages() {
            try {
                const resp = await fetch('/api/messages');
                const data = await resp.json();
                const container = document.getElementById('messages');
                if (data.length === 0) {
                    container.innerHTML = '<p style="color:#6c7086;">暂无留言，来做第一个留言的人吧！</p>';
                    return;
                }
                container.innerHTML = data.map(m => `
                    <div class="msg clearfix" id="msg-${m.id}">
                        <button class="del-btn" onclick="deleteMessage(${m.id})">删除</button>
                        <div class="meta">
                            <span class="author">${escapeHtml(m.author)}</span>
                            <span>${m.created_at}</span>
                        </div>
                        <div class="content">${escapeHtml(m.content)}</div>
                    </div>
                `).join('');
            } catch (e) {
                console.error('加载留言失败:', e);
            }
        }

        async function postMessage() {
            const author = document.getElementById('author').value.trim();
            const content = document.getElementById('content').value.trim();
            if (!author || !content) {
                document.getElementById('postStatus').textContent = '请填写昵称和内容';
                return;
            }
            const status = document.getElementById('postStatus');
            status.textContent = '发送中...';
            try {
                const resp = await fetch('/api/messages', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ author, content })
                });
                const data = await resp.json();
                if (resp.ok) {
                    status.textContent = '留言成功！';
                    document.getElementById('author').value = '';
                    document.getElementById('content').value = '';
                    loadMessages();
                } else {
                    status.textContent = '错误: ' + (data.error || '未知错误');
                }
            } catch (e) {
                status.textContent = '网络错误: ' + e.message;
            }
        }

        async function deleteMessage(id) {
            if (!confirm('确定删除这条留言吗？')) return;
            try {
                await fetch('/api/message/' + id, { method: 'DELETE' });
                loadMessages();
            } catch (e) {
                console.error('删除失败:', e);
            }
        }

        function escapeHtml(text) {
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }

        // 初始加载 + 每 5 秒自动刷新
        loadMessages();
        setInterval(loadMessages, 5000);
    </script>
</body>
</html>)HTML";
			file.close();
			LOG_INFO("Created guestbook.html with MySQL-backed RESTful demo");
		}
	}

    MainWindow mainWindow;
    mainWindow.show();
    LOG_INFO("Application started, UI displayed");

    return app.exec();
}