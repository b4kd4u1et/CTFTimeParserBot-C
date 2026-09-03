# CTFTimeParserBot-C

> [Қазақша](#қазақша) · [Русский](#русский) · [English](#english)

A from-scratch **C11** rewrite of [CTFTimeParser](https://github.com/b4kd4u1et/CTFTimeParser) (originally PHP + MySQL). Same behaviour, same database schema, same Telegram message format — implemented with no scripting-language runtime: just libcurl, a MySQL/MariaDB client library, and a small hand-written JSON parser, compiled to two static binaries (`parser`, `publisher`) that you run from cron.

---

## Қазақша

**CTFTimeParserBot-C** — [CTFTime](https://ctftime.org/) сайтынан алдағы CTF жарыстары туралы хабарландыруларды жинап, MySQL дерекқорына сақтайтын және Telegram супертопқа жариялайтын жеңіл, тәуелділіксіз C құралы.

### Мүмкіндіктер

- CTFTime API арқылы оқиғаларды автоматты жинау (libcurl)
- Екі сатылы буфер: деректер қайталанбауы үшін алдын ала сүзгіден өтеді
- XSS, SSTI, SQLi, SSRF және күдікті URL-дерге қарсы мазмұн қауіпсіздігі тексерулері (POSIX regex + қолмен жазылған URL/IP валидациясы)
- Қауіпті оқиғалар `is_safe=0` белгісімен сақталып, жариялаудан ұсталады
- **Апталық дайджест** — дүйсенбі сайын келесі 14 күннің барлық оқиғалары
- **Күнделікті жаңартулар** — жаңа оқиғаларды толық форматта жариялау
- Сыртқы скрипт тілі жоқ — таза C11, libcurl, MySQL/MariaDB клиент кітапханасы
- Атомдық блокировка файлдары (`open`+`flock`) cron-процестерінің қабаттасуын болдырмайды
- Файлдық логтау, 5 МБ-тан асқанда автоматты ротация
- `valgrind` арқылы жад ағуларына тексерілген (parser/publisher екеуі де таза)

### Талаптар

- POSIX жүйе (Linux), C11 компиляторы (`gcc`/`clang`), `make`
- `libcurl` дамыту тақырыптары (Debian/Ubuntu: `libcurl4-openssl-dev`)
- MySQL/MariaDB клиент кітапханасы (Debian/Ubuntu: `libmariadb-dev`, немесе `libmysqlclient-dev`)
- MySQL 8.0+ немесе MariaDB 10.5+ сервері

### Орнату

#### 1. Тәуелділіктерді орнату (Debian/Ubuntu мысалы)

```bash
sudo apt-get install build-essential libcurl4-openssl-dev libmariadb-dev
```

#### 2. Құрастыру

```bash
make
# -> bin/parser, bin/publisher
```

#### 3. Дерекқор және пайдаланушы жасау

```sql
CREATE DATABASE ctftimeparser CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE USER 'ctfparser'@'localhost' IDENTIFIED BY 'strong_password';
GRANT SELECT, INSERT, UPDATE, DELETE ON ctftimeparser.* TO 'ctfparser'@'localhost';
```

Схеманы қолдану:

```bash
mysql -u ctfparser -p ctftimeparser < schema.sql
```

#### 4. Telegram боты жасау

1. Telegram-да [@BotFather](https://t.me/BotFather) ашып, `/newbot` жіберіңіз
2. Алынған **бот токенін** көшіріңіз
3. Ботты супертопқа *хабар жариялау* рұқсатымен **әкімші** ретінде қосыңыз
4. Топ параметрлерінде **Topics** қосып, хабарландырулар тақырыбын жасаңыз
5. Сол тақырыптан кез келген хабарды [@JsonDumpBot](https://t.me/JsonDumpBot)-қа жіберіңіз — `message_thread_id` мәнін табыңыз

#### 5. Конфигурация

```bash
cp config.ini.sample config.ini
```

`config.ini` файлын толтырыңыз (INI пішімі, PHP массивінің орнына):

```ini
[db]
user = ctfparser
pass = strong_password
...

[telegram]
bot_token = 123456:ABC-your-token
chat_id   = -1001234567890
thread_id = 42
```

#### 6. Cron арқылы жоспарлау

Екілік файлдар өз каталогынан іске қосылған кезде `config.ini` файлын ағымдағы каталогтан іздейді, сондықтан жол көрсету ұсынылады:

```
# CTFTime-дан оқиғаларды әр 6 сағат сайын жинау
0 */6 * * * /path/to/CTFTimeParserBot-C/bin/parser /path/to/CTFTimeParserBot-C/config.ini

# Telegram-ға күн сайын 07:00-де жариялау
0 7   * * * /path/to/CTFTimeParserBot-C/bin/publisher /path/to/CTFTimeParserBot-C/config.ini
```

### Жоба құрылымы

```
CTFTimeParserBot-C/
├── Makefile                   # `make` -> bin/parser, bin/publisher
├── config.ini                 # Баптаулар мен кіру деректері (gitignored)
├── config.ini.sample          # Конфигурация үлгісі
├── schema.sql                 # Дерекқор схемасы (PHP нұсқасымен бірдей)
├── include/                   # Жалпыға ортақ тақырыптар (.h)
├── src/
│   ├── util.c                 # UTF-8, жол, IP/SSRF, уақыт көмекшілері
│   ├── json.c                 # Тәуелділіксіз JSON талдаушы/кодтаушы
│   ├── config.c                # INI конфигурация жүктеушісі
│   ├── log.c                   # Ротацияланатын файлдық логтау
│   ├── lock.c                  # Атомдық lock файлы (open+flock)
│   ├── http.c                  # libcurl GET/POST орауышы
│   ├── event.c                  # ctf_events жолын білдіретін struct
│   ├── content_security.c      # Санитизация және қауіпсіздік тексерулері
│   ├── ctftime_client.c         # CTFTime API клиенті
│   ├── db.c                     # MySQL/MariaDB C API орауышы
│   ├── formatter.c              # Telegram HTML хабар форматтаушы
│   ├── telegram_bot.c           # Telegram Bot API клиенті
│   ├── parser_main.c            # `parser` бинарының main()
│   └── publisher_main.c         # `publisher` бинарының main()
└── logs/
    ├── parser.log              # Парсер логы (5 МБ кезінде ротация)
    └── publisher.log           # Паблишер логы (5 МБ кезінде ротация)
```

### Дерекқор схемасы, жалпы алгоритм, хабар форматтары

PHP нұсқасымен бірдей — толық сипаттама үшін төмендегі **English** бөлімін немесе `schema.sql` файлын қараңыз.

### Ақаулықтарды жою

**Құрастыру кезінде `curl/curl.h: No such file or directory`**
```bash
sudo apt-get install libcurl4-openssl-dev
```

**Құрастыру кезінде `mysql.h: No such file or directory`**
```bash
sudo apt-get install libmariadb-dev   # немесе libmysqlclient-dev
```

**`config: cannot open 'config.ini'`**
- Екілік файлды дұрыс жұмыс каталогынан іске қосыңыз немесе жолды бірінші аргумент ретінде беріңіз: `./bin/parser /path/to/config.ini`

**`db: connection failed`**
- `config.ini` ішіндегі `[db]` деректерін тексеріңіз
- Пайдаланушы артықшылықтарын растаңыз: `SHOW GRANTS FOR 'ctfparser'@'localhost';`

**`Could not create lock file`**
- `/tmp` каталогының процеске жазу рұқсаты бар екенін тексеріңіз

**`Another instance is already running`**
- `flock()` процесс аяқталғанда автоматты босатылады — ағымдағы іске қосу аяқталғанша күтіңіз

---

## Русский

**CTFTimeParserBot-C** — переписанный с нуля на **C11** аналог [CTFTimeParser](https://github.com/b4kd4u1et/CTFTimeParser) (изначально PHP + MySQL). То же поведение, та же схема БД, тот же формат сообщений Telegram — но без какого-либо скриптового рантайма: только libcurl, клиентская библиотека MySQL/MariaDB и небольшой самописный JSON-парсер, собираемые в два статических бинарника (`parser`, `publisher`), запускаемых через cron.

### Возможности

- Автоматический сбор событий через CTFTime API (libcurl)
- Двухэтапный буфер: дедупликация перед загрузкой деталей
- Проверки безопасности контента: XSS, SSTI, SQLi, SSRF, подозрительные URL (POSIX regex + собственная валидация URL/IP)
- Небезопасные события сохраняются с флагом `is_safe=0` и не публикуются
- **Еженедельный дайджест** — каждый понедельник, список всех событий на 14 дней
- **Ежедневные обновления** — полный пост для каждого нового события
- Без скриптового рантайма — чистый C11, libcurl, клиент MySQL/MariaDB
- Атомарные блокировки (`open`+`flock`) предотвращают параллельный запуск cron-задач
- Файловое логирование с автоматической ротацией при 5 МБ
- Проверено `valgrind` на утечки памяти (parser и publisher — чисто)

### Требования

- POSIX-система (Linux), компилятор C11 (`gcc`/`clang`), `make`
- Заголовки разработки `libcurl` (Debian/Ubuntu: `libcurl4-openssl-dev`)
- Клиентская библиотека MySQL/MariaDB (Debian/Ubuntu: `libmariadb-dev`, либо `libmysqlclient-dev`)
- Сервер MySQL 8.0+ или MariaDB 10.5+

### Установка

#### 1. Установка зависимостей (пример для Debian/Ubuntu)

```bash
sudo apt-get install build-essential libcurl4-openssl-dev libmariadb-dev
```

#### 2. Сборка

```bash
make
# -> bin/parser, bin/publisher
```

#### 3. Создание базы данных и пользователя

```sql
CREATE DATABASE ctftimeparser CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

CREATE USER 'ctfparser'@'localhost' IDENTIFIED BY 'strong_password';
GRANT SELECT, INSERT, UPDATE, DELETE ON ctftimeparser.* TO 'ctfparser'@'localhost';
```

Применить схему:

```bash
mysql -u ctfparser -p ctftimeparser < schema.sql
```

#### 4. Создание Telegram-бота

1. Откройте [@BotFather](https://t.me/BotFather) в Telegram и отправьте `/newbot`
2. Скопируйте полученный **токен бота**
3. Добавьте бота в супергруппу как **администратора** с правом публикации сообщений
4. Включите **Topics** в настройках группы, создайте тему для анонсов
5. Перешлите любое сообщение из этой темы боту [@JsonDumpBot](https://t.me/JsonDumpBot) — найдите значение `message_thread_id`

#### 5. Конфигурация

```bash
cp config.ini.sample config.ini
```

Заполните `config.ini` (формат INI вместо PHP-массива):

```ini
[db]
user = ctfparser
pass = strong_password
...

[telegram]
bot_token = 123456:ABC-your-token
chat_id   = -1001234567890
thread_id = 42
```

#### 6. Запуск через cron

Бинарники ищут `config.ini` в текущем рабочем каталоге, поэтому в cron лучше передавать путь явно:

```
# Собирать события с CTFTime каждые 6 часов
0 */6 * * * /path/to/CTFTimeParserBot-C/bin/parser /path/to/CTFTimeParserBot-C/config.ini

# Публиковать в Telegram каждый день в 07:00
0 7   * * * /path/to/CTFTimeParserBot-C/bin/publisher /path/to/CTFTimeParserBot-C/config.ini
```

### Структура проекта

```
CTFTimeParserBot-C/
├── Makefile                   # `make` -> bin/parser, bin/publisher
├── config.ini                 # Настройки и учётные данные (gitignored)
├── config.ini.sample          # Шаблон конфигурации
├── schema.sql                 # Схема БД (идентична PHP-версии)
├── include/                   # Общие заголовки (.h)
├── src/
│   ├── util.c                 # UTF-8, строки, IP/SSRF, время
│   ├── json.c                 # Независимый JSON-парсер/энкодер
│   ├── config.c                # Загрузчик INI-конфигурации
│   ├── log.c                   # Ротируемое файловое логирование
│   ├── lock.c                  # Атомарный lock-файл (open+flock)
│   ├── http.c                  # Обёртка над libcurl (GET/POST)
│   ├── event.c                  # struct для строки ctf_events
│   ├── content_security.c      # Санитизация и проверки безопасности
│   ├── ctftime_client.c         # Клиент CTFTime API
│   ├── db.c                     # Обёртка над MySQL/MariaDB C API
│   ├── formatter.c              # Форматтер HTML-сообщений Telegram
│   ├── telegram_bot.c           # Клиент Telegram Bot API
│   ├── parser_main.c            # main() бинарника `parser`
│   └── publisher_main.c         # main() бинарника `publisher`
└── logs/
    ├── parser.log              # Лог парсера (ротация при 5 МБ)
    └── publisher.log           # Лог паблишера (ротация при 5 МБ)
```

### Общий алгоритм

```
Cron (каждые 6 часов)
    │
    ▼
parser запускается
    │
    ├─► Проверка блокировки (/tmp/ctftimeparser.lock)
    │       Другой процесс работает → немедленный выход (код 0)
    │
    ├─► Инициализация: config.ini → db_connect() + libcurl
    │
    ├─► Шаг 1: Сбор ID
    │       CTFTime API (следующие N дней) → parser_buffer (INSERT IGNORE)
    │
    ├─► Шаг 2: Дедупликация
    │       Удалить из parser_buffer ID, уже есть в ctf_events
    │
    ├─► Шаг 3: Загрузка и сохранение (для каждого нового ID)
    │       │
    │       ├─► CTFTime API → полные данные события (JSON)
    │       ├─► ID берётся из пути запроса (не из тела ответа) — защита от спуфинга
    │       ├─► content_security_sanitize()
    │       │       strip_tags + htmlspecialchars-эквивалент (XSS)
    │       │       Проверка паттернов SSTI/SQLi (POSIX regex)
    │       │       Валидация схемы URL + проверка SSRF (приватные/резервные диапазоны)
    │       │       Ограничения длины полей
    │       ├─► Санитизация не прошла → удалить из буфера, пропустить
    │       ├─► is_safe=0 → сохранить в ctf_events (не публикуется)
    │       ├─► is_safe=1 → сохранить в ctf_events (готово к публикации)
    │       └─► Пауза между запросами (настраивается)
    │
    ├─► Запись в лог (ротация при превышении 5 МБ)
    │
    └─► Блокировка снимается (при краше — ОС снимает автоматически)

publisher (cron, каждый день в 07:00)
    │
    ├─► Проверка блокировки (/tmp/ctftimepublisher.lock)
    │
    ├─► Понедельник → Еженедельный дайджест
    │       db_get_upcoming_events(14) → formatter_digest() → 1..N частей (≤4096 симв.)
    │       posted_at НЕ устанавливается (события появятся в ежедневных обновлениях)
    │
    └─► Вт–Вс → Ежедневные обновления
            db_get_unpublished_events() (is_safe=1 AND posted_at IS NULL)
            formatter_event() → полное HTML-сообщение → telegram_send_message()
            Успех → posted_at = NOW(); неудача → остаётся неопубликованным, повтор при следующем запуске
```

**Формат дайджеста (понедельник):**
```
📋 CTF Events — next 14 days
26 Mar – 09 Apr 2026

• SomeCTF 2026
  📅 28 Mar — 30 Mar | Jeopardy | Online
```

**Формат ежедневного обновления:**
```
🚩 SomeCTF 2026

📅 28 Mar — 30 Mar 2026 (UTC)
🏆 Jeopardy | Weight: 25.50
🌐 Online

Краткое описание события…

🔗 Event site  ·  CTFTime
```

### Схема базы данных

Идентична оригинальной PHP-версии — см. `schema.sql`.

#### `parser_buffer`

| Столбец | Тип | Описание |
|---------|-----|----------|
| `event_id` | INT UNSIGNED PK | ID события CTFTime |
| `created_at` | DATETIME | Время создания записи |

#### `ctf_events`

| Столбец | Тип | Описание |
|---------|-----|----------|
| `id` | INT UNSIGNED PK | ID события CTFTime |
| `title` | VARCHAR(255) | Название события |
| `url` | VARCHAR(512) | Официальный сайт события |
| `ctftime_url` | VARCHAR(512) | Страница события на CTFTime |
| `start_time` | DATETIME | Начало (UTC) |
| `finish_time` | DATETIME | Конец (UTC) |
| `format` | VARCHAR(64) | Jeopardy / Attack-Defense / и др. |
| `weight` | DECIMAL(8,5) | Рейтинговый вес CTFTime |
| `onsite` | TINYINT(1) | 1 = очное мероприятие |
| `location` | VARCHAR(255) | Город/страна для очных событий |
| `description` | TEXT | Описание события |
| `logo_url` | VARCHAR(512) | URL логотипа |
| `is_safe` | TINYINT(1) | 0 = помечено проверкой безопасности |
| `posted_at` | DATETIME | NULL = ещё не опубликовано в Telegram |
| `created_at` | DATETIME | Время создания записи |

### Безопасность

Порт защитных мер оригинала (аудит по **[OWASP Top 10:2025](https://owasp.org/Top10/2025/)**):

| OWASP | Угроза | Защита |
|---|---|---|
| A01 | Broken Access Control / SSRF | Редиректы отключены во всех HTTP-запросах (`CURLOPT_FOLLOWLOCATION=0`); только HTTPS; URL API захардкожен и дополнительно проверяется на домен; учётные данные в URL отклоняются; проверка приватных/резервных IPv4- и IPv6-диапазонов (`is_internal_host()` в `util.c`) |
| A02 | Security Misconfiguration | Выделенный пользователь БД с минимальными привилегиями; `config.ini` исключён из VCS; токен бота не пишется в логи |
| A05 | Injection — SQLi | Подготовленные выражения MySQL C API (`mysql_stmt_*`) повсюду; никакой интерполяции пользовательских данных; дополнительная regex-детекция как второй рубеж |
| A05 | Injection — SSTI | Regex-детекция паттернов `{{ }}`, `{% %}`, `<% %>`, `${}`, `#{}` (POSIX ERE) |
| A05 | Injection — XSS | Собственная реализация strip-tags + HTML-экранирование (`&`, `<`, `>`, `"`, `'`) на всех строковых полях до сохранения и вывода |
| A06 | Insecure Design | Нет блокирующего DNS-резолвинга; белый список схем URL (`http`/`https`); ограничения длины полей (в UTF-8 codepoints, как и в PHP-версии) |
| A08 | Data Integrity | ID события берётся из пути запроса, а не из тела ответа; ограничение глубины вложенности JSON |
| A09 | Security Logging Failures | Структурированный лог с уровнем и временной меткой; ротация при 5 МБ; отдельные логи для каждого компонента; запись в лог защищена `flock()` от чередования строк при параллельной записи |
| A10 | Mishandling of Exceptional Conditions | Атомарная блокировка `open(O_CREAT)+flock(LOCK_EX\|LOCK_NB)` — нет гонки TOCTOU; ОС снимает блокировку при краше; обработка Telegram 429 с `retry_after` |

Проверка памяти: обе программы прогнаны под `valgrind --leak-check=full` по всем веткам (успех/ошибка API, будни/понедельник, лок занят/свободен) без утечек и ошибок доступа к памяти.

### Логирование

Формат логов идентичен PHP-версии, каждый компонент пишет в свой файл с ротацией при 5 МБ:

**`logs/parser.log`**
```
[2026-03-26 12:00:00] [INFO] Fetching event list [2026-03-26 - 2026-04-09]
[2026-03-26 12:00:01] [INFO] Received 14 event IDs from API.
[2026-03-26 12:00:01] [INFO] Buffer cleaned (removed already-known events).
[2026-03-26 12:00:01] [INFO] 3 new event(s) to process.
[2026-03-26 12:00:02] [INFO] Event #2345 saved: "SomeCTF 2026" (safe=1)
[2026-03-26 12:00:04] [INFO] Done. Saved: 3 | Unsafe (stored): 0 | Skipped: 0
```

**`logs/publisher.log`**
```
[2026-03-30 07:00:00] [INFO] Daily update: 3 event(s) to publish.
[2026-03-30 07:00:01] [INFO] Published event #2345: "SomeCTF 2026"
[2026-03-30 07:00:03] [INFO] Published event #2346: "AnotherCTF 2026"
[2026-03-30 07:00:05] [INFO] Daily update done. Sent: 3 | Failed: 0
```

### Отличия от PHP-версии

- Конфигурация — INI-файл (`config.ini`) вместо PHP-массива (`config.php`).
- JSON, HTTP и MySQL-клиент реализованы напрямую поверх `libcurl`/MySQL C API вместо PDO/cURL-обёрток PHP; внешнее поведение (запросы, заголовки, тайм-ауты, отключённые редиректы, проверка TLS) сохранено один в один.
- Вместо `mb_substr`/`mb_strlen` — собственные функции подсчёта/обрезки по UTF-8 codepoint (`utf8_strlen`, `utf8_substr_alloc`), дающие тот же результат.
- Результат работы (два бинарника без рантайма) не требует установленного PHP на целевом сервере.

### Устранение неполадок

**При сборке `curl/curl.h: No such file or directory`**
```bash
sudo apt-get install libcurl4-openssl-dev
```

**При сборке `mysql.h: No such file or directory`**
```bash
sudo apt-get install libmariadb-dev   # или libmysqlclient-dev
```

**`config: cannot open 'config.ini'`**
- Запускайте бинарник из каталога с `config.ini`, либо передайте путь первым аргументом: `./bin/parser /path/to/config.ini`

**`db: connection failed`**
- Проверьte данные в `[db]` секции `config.ini`
- Убедитесь в правах пользователя: `SHOW GRANTS FOR 'ctfparser'@'localhost';`

**`Could not create lock file`**
- Проверьте права на запись в `/tmp` для пользователя, от имени которого выполняется cron

**Publisher ничего не отправляет**
- Сначала запустите `parser`, чтобы заполнить `ctf_events`
- Убедитесь, что есть строки с `is_safe = 1` и `posted_at IS NULL`
- Проверьте токен бота и chat/thread ID в `config.ini`

**`Another instance is already running`**
- `flock()` снимается автоматически при завершении процесса — дождитесь окончания текущего запуска

---

## English

**CTFTimeParserBot-C** is a from-scratch **C11** rewrite of [CTFTimeParser](https://github.com/b4kd4u1et/CTFTimeParser) (originally PHP + MySQL). Same behaviour, same database schema, same Telegram message format — but with no scripting-language runtime: just libcurl, a MySQL/MariaDB client library, and a small hand-written JSON parser, compiled into two static binaries (`parser`, `publisher`) that you schedule via cron.

### Features

- Fetches events via the CTFTime public API (libcurl)
- Two-stage buffer pipeline — deduplication before fetching details
- Content security checks: XSS, SSTI, SQLi, SSRF, suspicious URLs (POSIX regex + a hand-written URL/IP validator)
- Unsafe events are stored with `is_safe=0` and held back from publication
- **Weekly digest** every Monday — compact list of all events for the next 14 days
- **Daily updates** — individual full-detail posts for new events
- No scripting-language runtime — pure C11, libcurl, MySQL/MariaDB client
- Atomic lock files (`open`+`flock`) prevent overlapping cron runs
- File-based logging with automatic rotation at 5 MB
- Verified leak-free under `valgrind` (both binaries, every branch)

### Requirements

- A POSIX system (Linux), a C11 compiler (`gcc`/`clang`), `make`
- `libcurl` development headers (Debian/Ubuntu: `libcurl4-openssl-dev`)
- A MySQL/MariaDB client library (Debian/Ubuntu: `libmariadb-dev`, or `libmysqlclient-dev`)
- MySQL 8.0+ or MariaDB 10.5+ server

### Setup

#### 1. Install build dependencies (Debian/Ubuntu example)

```bash
sudo apt-get install build-essential libcurl4-openssl-dev libmariadb-dev
```

#### 2. Build

```bash
make
# -> bin/parser, bin/publisher
```

The Makefile auto-detects `libcurl` and `libmariadb`/`libmysqlclient` via `pkg-config` / `mariadb_config` / `mysql_config`.

#### 3. Create the database and user

```sql
CREATE DATABASE ctftimeparser CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- Use a dedicated user with minimum required privileges, not root
CREATE USER 'ctfparser'@'localhost' IDENTIFIED BY 'strong_password';
GRANT SELECT, INSERT, UPDATE, DELETE ON ctftimeparser.* TO 'ctfparser'@'localhost';
```

Apply the schema:

```bash
mysql -u ctfparser -p ctftimeparser < schema.sql
```

#### 4. Create a Telegram bot

1. Open [@BotFather](https://t.me/BotFather) in Telegram and send `/newbot`
2. Copy the **bot token** you receive
3. Add the bot to your supergroup as an **administrator** with *Post Messages* permission
4. Enable **Topics** in the group settings, then create the announcements topic
5. Forward any message from that topic to [@JsonDumpBot](https://t.me/JsonDumpBot) — find `message_thread_id` in the output

#### 5. Configure

```bash
cp config.ini.sample config.ini
```

Fill in `config.ini` (an INI file, replacing PHP's config array):

```ini
[db]
host    = localhost
port    = 3306
name    = ctftimeparser
user    = ctfparser
pass    = strong_password
charset = utf8mb4

[parser]
days_ahead             = 14
events_limit           = 100
request_timeout        = 10
sleep_between_requests = 1

[telegram]
bot_token              = 123456:ABC-your-token
chat_id                = -1001234567890
thread_id               = 42
sleep_between_messages = 2

[logging]
log_file           = logs/parser.log
publisher_log_file = logs/publisher.log
```

#### 6. Schedule via cron

Both binaries look for `config.ini` in the current working directory, so pass an explicit path in cron:

```
# Fetch new events from CTFTime every 6 hours
0 */6 * * * /path/to/CTFTimeParserBot-C/bin/parser /path/to/CTFTimeParserBot-C/config.ini

# Publish to Telegram every day at 07:00
0 7   * * * /path/to/CTFTimeParserBot-C/bin/publisher /path/to/CTFTimeParserBot-C/config.ini
```

### Project Structure

```
CTFTimeParserBot-C/
├── Makefile                   # `make` -> bin/parser, bin/publisher
├── config.ini                 # Credentials and settings (gitignored)
├── config.ini.sample          # Configuration template
├── schema.sql                 # Database schema (identical to the PHP version)
├── include/                   # Shared headers (.h)
├── src/
│   ├── util.c                 # UTF-8, string, IP/SSRF, and time helpers
│   ├── json.c                 # Dependency-free JSON parser/encoder
│   ├── config.c                # INI config loader
│   ├── log.c                   # Rotating file logger
│   ├── lock.c                  # Atomic lock file (open+flock)
│   ├── http.c                  # libcurl GET/POST wrapper
│   ├── event.c                  # struct mirroring a ctf_events row
│   ├── content_security.c      # Sanitization and threat detection
│   ├── ctftime_client.c         # CTFTime API client
│   ├── db.c                     # MySQL/MariaDB C API wrapper
│   ├── formatter.c              # Telegram HTML message formatter
│   ├── telegram_bot.c           # Telegram Bot API client
│   ├── parser_main.c            # `parser` binary's main()
│   └── publisher_main.c         # `publisher` binary's main()
└── logs/
    ├── parser.log              # Parser log (auto-created, rotates at 5 MB)
    └── publisher.log           # Publisher log (auto-created, rotates at 5 MB)
```

### Overall Algorithm

```
Cron (every 6 hours)
    │
    ▼
parser runs
    │
    ├─► Lock check (/tmp/ctftimeparser.lock)
    │       Another instance running → exit immediately (code 0)
    │
    ├─► Bootstrap: config.ini → db_connect() + libcurl
    │
    ├─► Step 1: Collect IDs
    │       CTFTime API (next N days) → parser_buffer (INSERT IGNORE)
    │
    ├─► Step 2: Deduplicate
    │       Remove from parser_buffer any IDs already in ctf_events
    │
    ├─► Step 3: Fetch and store (for each new ID)
    │       │
    │       ├─► CTFTime API → full event details (JSON)
    │       ├─► Event ID taken from the request path (not response body) — anti-spoofing
    │       ├─► content_security_sanitize()
    │       │       strip-tags + HTML-escape equivalent (XSS)
    │       │       SSTI / SQLi pattern detection (POSIX regex)
    │       │       URL scheme validation + SSRF checks (private/reserved ranges)
    │       │       Field length limits
    │       ├─► Sanitization failed → delete from buffer, skip
    │       ├─► is_safe=0 → store in ctf_events (withheld from publication)
    │       ├─► is_safe=1 → store in ctf_events (ready to publish)
    │       └─► Configurable pause between requests
    │
    ├─► Write to log (rotate when file exceeds 5 MB)
    │
    └─► Lock released (OS releases automatically on crash)

publisher (cron, daily at 07:00)
    │
    ├─► Lock check (/tmp/ctftimepublisher.lock)
    │
    ├─► Monday → Weekly digest
    │       db_get_upcoming_events(14) → formatter_digest() → 1..N message parts (≤4096 chars)
    │       posted_at is NOT set (events still appear in daily updates)
    │
    └─► Tue-Sun → Daily updates
            db_get_unpublished_events() (is_safe=1 AND posted_at IS NULL)
            formatter_event() → full Telegram HTML message → telegram_send_message()
            Success → posted_at = NOW(); failure → left unpublished, retried next run
```

**Monday digest format:**
```
📋 CTF Events — next 14 days
26 Mar – 09 Apr 2026

• SomeCTF 2026
  📅 28 Mar — 30 Mar | Jeopardy | Online
```

**Daily update format:**
```
🚩 SomeCTF 2026

📅 28 Mar — 30 Mar 2026 (UTC)
🏆 Jeopardy | Weight: 25.50
🌐 Online

Brief description…

🔗 Event site  ·  CTFTime
```

### Database Schema

Identical to the original PHP version — see `schema.sql`.

#### `parser_buffer`

| Column | Type | Description |
|--------|------|-------------|
| `event_id` | INT UNSIGNED PK | CTFTime event ID |
| `created_at` | DATETIME | Row creation time |

#### `ctf_events`

| Column | Type | Description |
|--------|------|-------------|
| `id` | INT UNSIGNED PK | CTFTime event ID |
| `title` | VARCHAR(255) | Event name |
| `url` | VARCHAR(512) | Official event website |
| `ctftime_url` | VARCHAR(512) | CTFTime event page |
| `start_time` | DATETIME | Start (UTC) |
| `finish_time` | DATETIME | End (UTC) |
| `format` | VARCHAR(64) | Jeopardy / Attack-Defense / etc. |
| `weight` | DECIMAL(8,5) | CTFTime rating weight |
| `onsite` | TINYINT(1) | 1 = on-site event |
| `location` | VARCHAR(255) | City/country for on-site events |
| `description` | TEXT | Event description |
| `logo_url` | VARCHAR(512) | Event logo URL |
| `is_safe` | TINYINT(1) | 0 = flagged by security checks |
| `posted_at` | DATETIME | NULL = not yet published to Telegram |
| `created_at` | DATETIME | Row creation time |

Events with `is_safe = 0` are stored but **not published** until manually reviewed.

### Security

A direct port of the original's defences, audited against **[OWASP Top 10:2025](https://owasp.org/Top10/2025/)**:

| OWASP | Threat | Defence |
|---|---|---|
| A01 | Broken Access Control / SSRF | Redirects disabled on every HTTP request (`CURLOPT_FOLLOWLOCATION=0`); HTTPS-only; API base URL hardcoded and re-validated against its expected domain; credentials-in-URL rejected; private/reserved IPv4 and IPv6 range check on literal IPs (`is_internal_host()` in `util.c`) |
| A02 | Security Misconfiguration | Dedicated DB user with minimum privileges; `config.ini` excluded from VCS; bot token never written to logs |
| A05 | Injection — SQLi | MySQL C API prepared statements (`mysql_stmt_*`) throughout; no user-data interpolation; supplementary regex detection as a second layer |
| A05 | Injection — SSTI | Regex detection of `{{ }}`, `{% %}`, `<% %>`, `${}`, `#{}` patterns (POSIX ERE) |
| A05 | Injection — XSS | Hand-written strip-tags + HTML-escaping (`&`, `<`, `>`, `"`, `'`) on all string fields before storage and before output |
| A06 | Insecure Design | No blocking DNS resolution; URL scheme whitelist (`http`/`https`); field length limits (counted in UTF-8 codepoints, matching the PHP version) |
| A08 | Data Integrity | Event ID overridden from the request path, not the response body; JSON nesting depth limited |
| A09 | Security Logging Failures | Structured log with level + timestamp; automatic 5 MB rotation; separate logs per component; log writes serialized with `flock()` so concurrent writers never interleave partial lines |
| A10 | Mishandling of Exceptional Conditions | Atomic lock via `open(O_CREAT)+flock(LOCK_EX\|LOCK_NB)` — no TOCTOU race; OS auto-releases the lock on crash; Telegram 429 handled with `retry_after` back-off |

Memory safety: both binaries were run under `valgrind --leak-check=full` across every branch (API success/failure, weekday/Monday, lock free/held) with zero leaks and zero memory errors.

### Logging

Log format is identical to the PHP version; each component writes to its own file, rotated automatically at 5 MB:

**`logs/parser.log`**
```
[2026-03-26 12:00:00] [INFO] Fetching event list [2026-03-26 - 2026-04-09]
[2026-03-26 12:00:01] [INFO] Received 14 event IDs from API.
[2026-03-26 12:00:01] [INFO] Buffer cleaned (removed already-known events).
[2026-03-26 12:00:01] [INFO] 3 new event(s) to process.
[2026-03-26 12:00:02] [INFO] Event #2345 saved: "SomeCTF 2026" (safe=1)
[2026-03-26 12:00:04] [INFO] Done. Saved: 3 | Unsafe (stored): 0 | Skipped: 0
```

**`logs/publisher.log`**
```
[2026-03-30 07:00:00] [INFO] Daily update: 3 event(s) to publish.
[2026-03-30 07:00:01] [INFO] Published event #2345: "SomeCTF 2026"
[2026-03-30 07:00:03] [INFO] Published event #2346: "AnotherCTF 2026"
[2026-03-30 07:00:05] [INFO] Daily update done. Sent: 3 | Failed: 0
```

### Differences from the PHP version

- Configuration is an INI file (`config.ini`) instead of a PHP array (`config.php`).
- JSON, HTTP, and the MySQL client are implemented directly on top of `libcurl` and the MySQL C API instead of PHP's PDO/cURL wrappers; the external behaviour (requests, headers, timeouts, disabled redirects, TLS verification) is preserved exactly.
- `mb_substr`/`mb_strlen` are replaced with hand-written UTF-8 codepoint counting/truncation (`utf8_strlen`, `utf8_substr_alloc`) that produce the same results.
- The build output is two runtime-free binaries — no PHP installation is required on the target server.

### Troubleshooting

**`curl/curl.h: No such file or directory` while building**
```bash
sudo apt-get install libcurl4-openssl-dev
```

**`mysql.h: No such file or directory` while building**
```bash
sudo apt-get install libmariadb-dev   # or libmysqlclient-dev
```

**`config: cannot open 'config.ini'`**
- Run the binary from the directory containing `config.ini`, or pass the path as the first argument: `./bin/parser /path/to/config.ini`

**`db: connection failed`**
- Verify the `[db]` credentials in `config.ini`
- Confirm grants: `SHOW GRANTS FOR 'ctfparser'@'localhost';`

**`Could not create lock file`**
- Check that `/tmp` is writable by the user cron runs as

**No events appear after running the parser**
- CTFTime API returns an empty list if no events are scheduled in the next N days — this is normal
- Check `logs/parser.log` for API errors

**Publisher sends nothing**
- Run `parser` first so `ctf_events` is populated
- Check that `is_safe = 1` and `posted_at IS NULL` for at least one row
- Verify the bot token and chat/thread IDs in `config.ini`

**`Another instance is already running`**
- `flock()` releases automatically when the process exits — wait for the current run to finish
