// 简易 HTTP 压测工具（基于 Boost.Asio）
// 用法: bench_client.exe <host> <port> <path> <connections> <requests_per_conn>
// 示例: bench_client.exe 127.0.0.1 8080 /index.html 100 100
#include <boost/asio.hpp>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <atomic>
#include <thread>
#include <vector>
#include <sstream>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// 全局统计
std::atomic<uint64_t> total_requests{ 0 };
std::atomic<uint64_t> total_success{ 0 };
std::atomic<uint64_t> total_fail{ 0 };
std::atomic<uint64_t> total_bytes{ 0 };
std::atomic<uint64_t> total_latency_us{ 0 };
std::chrono::steady_clock::time_point start_time;

// 单次 HTTP 请求
void doRequest(asio::io_context& io, const std::string& host,
               const std::string& port, const std::string& path,
               int requests) {
    for (int i = 0; i < requests; ++i) {
        try {
            tcp::socket socket(io);
            tcp::resolver resolver(io);
            auto endpoints = resolver.resolve(host, port);

            auto t1 = std::chrono::steady_clock::now();
            asio::connect(socket, endpoints);

            // 构建 HTTP 请求
            std::stringstream req;
            req << "GET " << path << " HTTP/1.1\r\n";
            req << "Host: " << host << ":" << port << "\r\n";
            req << "Connection: keep-alive\r\n";
            req << "\r\n";

            asio::write(socket, asio::buffer(req.str()));

            // 读取响应
            asio::streambuf response;
            boost::system::error_code ec;
            while (asio::read(socket, response, asio::transfer_at_least(1), ec)) {
                if (ec == asio::error::eof) break;
                if (ec) throw boost::system::system_error(ec);
            }

            auto t2 = std::chrono::steady_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

            total_latency_us.fetch_add(latency);
            total_bytes.fetch_add(response.size());
            total_success.fetch_add(1);

            socket.close();
        } catch (const std::exception&) {
            total_fail.fetch_add(1);
        }
        total_requests.fetch_add(1);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 6) {
        std::cerr << "用法: " << argv[0]
                  << " <host> <port> <path> <connections> <requests_per_conn>\n";
        std::cerr << "示例: " << argv[0]
                  << " 127.0.0.1 8080 /index.html 50 200\n";
        return 1;
    }

    std::string host = argv[1];
    std::string port = argv[2];
    std::string path = argv[3];
    int connections = std::stoi(argv[4]);
    int requests_per_conn = std::stoi(argv[5]);

    int total = connections * requests_per_conn;

    std::cout << "============ MiniWebServer 压测工具 ============\n";
    std::cout << "目标: " << host << ":" << port << path << "\n";
    std::cout << "并发连接: " << connections << "\n";
    std::cout << "每连接请求数: " << requests_per_conn << "\n";
    std::cout << "总请求数: " << total << "\n";
    std::cout << "================================================\n\n";

    start_time = std::chrono::steady_clock::now();

    // 启动并发连接
    std::vector<std::thread> threads;
    for (int i = 0; i < connections; ++i) {
        threads.emplace_back([&, host, port, path, requests_per_conn] {
            asio::io_context io;
            doRequest(io, host, port, path, requests_per_conn);
        });
    }

    // 等待完成
    for (auto& t : threads) t.join();

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    double elapsed_sec = elapsed / 1000.0;

    // 统计输出
    std::cout << "\n============ 压测结果 ============\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "总耗时:       " << elapsed_sec << " 秒\n";
    std::cout << "总请求数:     " << total_requests.load() << "\n";
    std::cout << "成功:         " << total_success.load() << "\n";
    std::cout << "失败:         " << total_fail.load() << "\n";
    std::cout << "总传输:       " << (total_bytes.load() / 1024.0 / 1024.0) << " MB\n";

    double qps = total_success.load() / elapsed_sec;
    std::cout << "QPS:          " << qps << " req/s\n";

    double bytes_per_sec = total_bytes.load() / elapsed_sec;
    if (bytes_per_sec > 1024 * 1024)
        std::cout << "吞吐量:       " << (bytes_per_sec / 1024 / 1024) << " MB/s\n";
    else if (bytes_per_sec > 1024)
        std::cout << "吞吐量:       " << (bytes_per_sec / 1024) << " KB/s\n";
    else
        std::cout << "吞吐量:       " << bytes_per_sec << " B/s\n";

    double avg_latency = (total_latency_us.load() / 1000.0) / std::max(total_success.load(), 1ULL);
    std::cout << "平均延迟:     " << avg_latency << " ms\n";
    std::cout << "=================================\n";

    return 0;
}