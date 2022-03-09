# Пример приема-передачи файлов по протоколу UDP
## Сборка
make
## udp_server
При запуске открывает UDP сокет, используя порт 8080.  
Ожидает входящих сообщений.
## udp_client
Запускается с параметрами <имя_файла> [адрес сервера ipv4 = 127.0.0.1] [порт = 8080].  
Открывает UDP сокет, считывает данные из файла и отправляет в сокет по указанному адресу и порту.
## Протокол
- перед отправкой данных клиент отправляет служебное стартовое сообщение и ожидает ответа от сервера;
- после получения ответа клиент начинает передачу сообщений с данными файла. Каждое такое сообщение начинается с заголовка фиксированной длины, в котором передается идентификатор файла;
- сервер сохраняет полученные данные в файл, имя которого формируется из: адреса клиента, порт клиента, идентификатор файла.
## Особенности реализации
- сервер имеет два потока: основной, работающий с сокетом, и поток для записи данных в файлы;
- обмен данными между потоками реализован с использованием очереди сообщений;
- для минимизации потери пакетов клиент посылает данные с задержкой, длительность которой определяется случайным образом из заданного интервала значений.

