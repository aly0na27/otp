# Имя исполняемого файла
TARGET = otp

# Компилятор C++
CXX = g++

# Флаги компилятора C++
# -Wall -Wextra: Включить все основные предупреждения
# -g: Добавить отладочную информацию
# -O2: Оптимизация второго уровня
# -std=c++17: Использовать стандарт C++17 (или c++11, c++14)
CXXFLAGS = -Wall -Wextra -g -O2 -std=c++17

# Библиотеки для линковки
# -pthread: для POSIX потоков
LDFLAGS = -pthread

# Исходные файлы
SRCS = otp.cpp

# Правило для сборки основной цели
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

# Правило для очистки скомпилированных файлов
clean:
	rm -f $(TARGET)

# Phony таргеты (не являются файлами)
.PHONY: all clean
