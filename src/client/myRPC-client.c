#include <stdio.h>                   // Ввод/вывод
#include <stdlib.h>                  // Функции malloc, free, exit и др.
#include <string.h>                  // Работа со строками
#include <unistd.h>                  // POSIX-функции, включая getopt, close
#include <json-c/json.h>            // Работа с JSON
#include <getopt.h>                 // Обработка длинных опций командной строки
#include <pwd.h>                    // Получение информации о пользователе
#include <sys/socket.h>             // Сокеты
#include <netinet/in.h>             // Структуры для IP-адресов
#include <arpa/inet.h>              // Преобразование IP-адресов
#include <errno.h>                  // Обработка ошибок

#define BUF_SIZE 4096  // Размер буфера для приёма данных от сервера

// Функция вывода справки по использованию клиента
void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Options:\n");
    printf("  -c, --command \"bash_command\"    Bash command to execute\n");
    printf("  -h, --host \"ip_addr\"             Server IP address\n");
    printf("  -p, --port PORT                  Server port\n");
    printf("  -s, --stream                     Use stream (TCP) socket\n");
    printf("  -d, --dgram                      Use datagram (UDP) socket\n");
    printf("      --help                       Show this help message\n");
}

int main(int argc, char *argv[]) {
    char *command = NULL, *host = NULL; // Переменные для команды и IP
    int port = 0, use_stream = 0, use_dgram = 0; // Порт и флаги типа сокета

    // Определение поддерживаемых опций (для getopt_long)
    static struct option long_options[] = {
        {"command", required_argument, 0, 'c'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"stream", no_argument, 0, 's'},
        {"dgram", no_argument, 0, 'd'},
        {"help", no_argument, 0, 0},
        {0, 0, 0, 0}
    };

    // Разбор командной строки
    int opt, option_index = 0;
    while ((opt = getopt_long(argc, argv, "c:h:p:sd", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c': command = strdup(optarg); break; // Сохраняем команду
            case 'h': host = strdup(optarg); break;    // Сохраняем IP-адрес
            case 'p': port = atoi(optarg); break;      // Преобразуем порт в число
            case 's': use_stream = 1; break;           // Включаем TCP
            case 'd': use_dgram = 1; break;            // Включаем UDP
            case 0: // Обработка --help
                if (strcmp(long_options[option_index].name, "help") == 0) {
                    print_usage(argv[0]);
                    exit(0);
                }
                break;
            default: // Неверный аргумент
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    // Проверка обязательных аргументов
    if (!command || !host || !port || (!use_stream && !use_dgram)) {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    // Получение имени текущего пользователя (например, "triplex")
    struct passwd *pw = getpwuid(getuid());
    if (!pw) {
        perror("getpwuid");
        exit(EXIT_FAILURE);
    }
    const char *username = pw->pw_name;

    // Создание JSON-запроса с логином и командой
    struct json_object *req = json_object_new_object();
    json_object_object_add(req, "login", json_object_new_string(username));
    json_object_object_add(req, "command", json_object_new_string(command));
    const char *json_str = json_object_to_json_string(req); // Преобразование в строку

    // Создание сокета: AF_INET — IPv4, SOCK_STREAM/TCP или SOCK_DGRAM/UDP
    int sockfd;
    struct sockaddr_in servaddr;
    sockfd = socket(AF_INET, (use_stream ? SOCK_STREAM : SOCK_DGRAM), 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Заполнение структуры с адресом сервера
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port); // Перевод порта в сетевой порядок
    inet_pton(AF_INET, host, &servaddr.sin_addr); // Преобразование IP-строки в бинарный вид

    if (use_stream) {
        // TCP: установка соединения
        if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
            perror("connect");
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        // Отправка JSON-запроса на сервер
        send(sockfd, json_str, strlen(json_str), 0);

        // Ожидание и приём ответа
        char buf[BUF_SIZE] = {0};
        ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            printf("Ответ сервера: %s\n", buf);
        } else {
            perror("recv");
        }

    } else {
        // UDP: установка таймаута 5 сек на приём ответа
        struct timeval tv = {5, 0};
        setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Отправка запроса через sendto (UDP)
        sendto(sockfd, json_str, strlen(json_str), 0, (struct sockaddr*)&servaddr, sizeof(servaddr));

        // Ожидание и приём ответа
        char buf[BUF_SIZE] = {0};
        socklen_t len = sizeof(servaddr);
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&servaddr, &len);
        if (n > 0) {
            buf[n] = '\0';
            printf("Ответ сервера: %s\n", buf);
        } else {
            // Обработка таймаута (ошибка EAGAIN или EWOULDBLOCK)
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr, "Timeout waiting for UDP response\n");
            } else {
                perror("recvfrom");
            }
        }
    }

    // Освобождение ресурсов
    close(sockfd);
    free(command);
    free(host);
    json_object_put(req); // Уменьшение счётчика ссылок JSON-объекта

    return 0;
}
