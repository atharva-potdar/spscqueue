#include <array>
#include <iostream>

template<typename T, std::size_t N>
class SPSCQueue {
    private:
        std::array<T, N> buffer{};
        std::size_t head = 0;
        std::size_t tail = 0;
    public:
        bool enqueue(T t) {
            if ((tail + 1) % N == head)
                return false; // Queue full
            buffer[tail] = std::move(t);
            tail = (tail + 1) % N;
            return true;
        }

        bool dequeue(T& t) {
            if (head == tail)
                return false;
            t = std::move(buffer[head]);
            head = (head + 1) % N;
            return true;
        }

        void front() {
            if (head != tail)
                std::cout << buffer[head] << "\n";
        }
};
