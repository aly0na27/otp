#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>

#if defined(__APPLE__)

#ifndef PTHREAD_BARRIER_SERIAL_THREAD
#define PTHREAD_BARRIER_SERIAL_THREAD 1
#endif

typedef int pthread_barrierattr_t;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int tripCount;
} pthread_barrier_t;

static int pthread_barrier_init(pthread_barrier_t* barrier, const pthread_barrierattr_t* attr, unsigned int count) {
    (void)attr;
    pthread_mutex_init(&barrier->mutex, 0);
    pthread_cond_init(&barrier->cond, 0);
    barrier->tripCount = count;
    barrier->count = 0;
    return 0;
}

static int pthread_barrier_destroy(pthread_barrier_t* barrier) {
    pthread_cond_destroy(&barrier->cond);
    pthread_mutex_destroy(&barrier->mutex);
    return 0;
}

static int pthread_barrier_wait(pthread_barrier_t* barrier) {
    pthread_mutex_lock(&barrier->mutex);
    ++(barrier->count);
    if (barrier->count >= barrier->tripCount) {
        barrier->count = 0;
        pthread_cond_broadcast(&barrier->cond);
        pthread_mutex_unlock(&barrier->mutex);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    } else {
        pthread_cond_wait(&barrier->cond, &barrier->mutex);
        pthread_mutex_unlock(&barrier->mutex);
        return 0;
    }
}
#endif

constexpr size_t MAX_FILE_SIZE = 1024 * 1024 * 512;

struct LCGParams {
    long long x_seed = 0;
    long long a_mult = 0;
    long long c_inc = 0;
    long long m_mod = 0;
    std::string input_file;
    std::string output_file;
};

struct LCGThreadArgs {
    const LCGParams* params;
    std::vector<unsigned char>* output_pad;
};

struct WorkerContext {
    unsigned char* text_chunk;
    unsigned char* pad_chunk;
    size_t chunk_size;
    pthread_barrier_t* barrier;
};

extern "C" void* lcg_generator_thread_entry(void* args);
extern "C" void* encryption_worker_thread_entry(void* args);

void* lcg_generator_thread_entry(void* args) {
    auto* lcg_args = static_cast<LCGThreadArgs*>(args);
    long long current_x = lcg_args->params->x_seed;
    const long long a = lcg_args->params->a_mult;
    const long long c = lcg_args->params->c_inc;
    const long long m = lcg_args->params->m_mod;
    std::vector<unsigned char>& pad = *(lcg_args->output_pad);

    for (size_t i = 0; i < pad.size(); ++i) {
        current_x = (a * current_x + c) % m;
        pad[i] = static_cast<unsigned char>(current_x & 0xFF);
    }
    return nullptr;
}

void* encryption_worker_thread_entry(void* args) {
    auto* context = static_cast<WorkerContext*>(args);
    for (size_t i = 0; i < context->chunk_size; ++i) {
        context->text_chunk[i] ^= context->pad_chunk[i];
    }
    pthread_barrier_wait(context->barrier);
    return nullptr;
}

int main(int argc, char* argv[]) {
    std::ios_base::sync_with_stdio(false);
    std::cout.tie(nullptr);

    LCGParams params;
    int opt;
    auto total_start = std::chrono::steady_clock::now();

    while ((opt = getopt(argc, argv, "i:o:x:a:c:m:")) != -1) {
        switch (opt) {
            case 'i': params.input_file = optarg; break;
            case 'o': params.output_file = optarg; break;
            case 'x': params.x_seed = std::stoll(optarg); break;
            case 'a': params.a_mult = std::stoll(optarg); break;
            case 'c': params.c_inc = std::stoll(optarg); break;
            case 'm': params.m_mod = std::stoll(optarg); break;
            default: break;
        }
    }

    std::cout << "--- Параметры запуска ---\n"
              << "Входной файл: " << params.input_file << "\n"
              << "Выходной файл: " << params.output_file << "\n"
              << "Параметры ЛКГ: x=" << params.x_seed << ", a=" << params.a_mult 
              << ", c=" << params.c_inc << ", m=" << params.m_mod << "\n"
              << "------------------------\n\n";

    auto stage_start = std::chrono::steady_clock::now();
    
    int in_fd = open(params.input_file.c_str(), O_RDONLY);
    struct stat st;
    fstat(in_fd, &st);
    size_t file_size = st.st_size;

    std::vector<unsigned char> input_data(file_size);
    read(in_fd, input_data.data(), file_size);
    close(in_fd);

    auto stage_end = std::chrono::steady_clock::now();
    std::cout << "1) Чтение файла заняло " 
              << std::chrono::duration<double>(stage_end - stage_start).count() << " секунд.\n";

    stage_start = std::chrono::steady_clock::now();
    std::vector<unsigned char> one_time_pad(file_size);

    pthread_t lcg_tid;
    LCGThreadArgs lcg_args = {&params, &one_time_pad};
    pthread_create(&lcg_tid, nullptr, lcg_generator_thread_entry, &lcg_args);
    pthread_join(lcg_tid, nullptr);
    
    stage_end = std::chrono::steady_clock::now();
    std::cout << "2) Генерация одноразового блокнота заняла "
              << std::chrono::duration<double>(stage_end - stage_start).count() << " секунд.\n";

    stage_start = std::chrono::steady_clock::now();
    unsigned int num_cores = std::thread::hardware_concurrency();
    if (num_cores == 0) num_cores = 1;
    std::cout << "Обнаружено " << num_cores << " ядер ЦП. Будет создано " << num_cores << " воркеров.\n";

    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, nullptr, num_cores + 1);

    std::vector<pthread_t> worker_threads(num_cores);
    std::vector<WorkerContext> worker_contexts(num_cores);
    
    size_t chunk_size = file_size / num_cores;
    size_t current_offset = 0;

    for (unsigned int i = 0; i < num_cores; ++i) {
        worker_contexts[i].text_chunk = input_data.data() + current_offset;
        worker_contexts[i].pad_chunk = one_time_pad.data() + current_offset;
        worker_contexts[i].barrier = &barrier;

        if (i == num_cores - 1) {
            worker_contexts[i].chunk_size = file_size - current_offset;
        } else {
            worker_contexts[i].chunk_size = chunk_size;
        }
        pthread_create(&worker_threads[i], nullptr, encryption_worker_thread_entry, &worker_contexts[i]);
        current_offset += chunk_size;
    }
    
    stage_end = std::chrono::steady_clock::now();
    std::cout << "3): Инициализация воркеров заняла " 
              << std::chrono::duration<double>(stage_end - stage_start).count() << " секунд.\n";
    
    stage_start = std::chrono::steady_clock::now();
    
    pthread_barrier_wait(&barrier);
    
    stage_end = std::chrono::steady_clock::now();
    std::cout << "4) Шифрование заняло " 
              << std::chrono::duration<double>(stage_end - stage_start).count() << " секунд.\n";

    stage_start = std::chrono::steady_clock::now();

    int out_fd = open(params.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(out_fd, input_data.data(), file_size);
    close(out_fd);

    stage_end = std::chrono::steady_clock::now();
    std::cout << "5) Запись результата в файл заняла "
              << std::chrono::duration<double>(stage_end - stage_start).count() << " секунд.\n";
    
    for (auto& th : worker_threads) {
        pthread_join(th, nullptr);
    }
    
    pthread_barrier_destroy(&barrier);
    
    auto total_end = std::chrono::steady_clock::now();
    std::cout << "\nОбщее время выполнения программы: " 
              << std::chrono::duration<double>(total_end - total_start).count() << " секунд.\n";

    return EXIT_SUCCESS;
}
