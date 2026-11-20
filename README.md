# Паралельна пошукова система з інтеграцією LLM

Курсова робота з дисципліни «Паралельні обчислення» (КПІ, ІПСА).

Пошуковий сервер на C++20: паралельна побудова інвертованого індексу власним пулом потоків,
власна потокобезпечна хеш-таблиця, паралельна обробка клієнтів, розширення запиту
через локальну LLM (Ollama), REST API і WebSocket-стрімінг етапів пошуку.

## Запуск

Потрібен лише Docker з Docker Compose v2 (Docker Desktop на Windows/macOS).

```bash
git clone https://github.com/obolentsevB/course_work_parallel_computing.git
cd course_work_parallel_computing
docker compose up --build
```

Більше нічого робити не потрібно. Compose піднімає чотири сервіси:

| Сервіс | Що робить | Скільки працює |
|---|---|---|
| `dataset` | генерує датасет (≈ 2.1 ГБ, ≈ 107 000 документів) у том `dataset` | перший запуск — кілька хвилин, далі — секунда (датасет уже є) |
| `ollama` | сервер LLM Ollama, окремий контейнер | постійно |
| `ollama-init` | завантажує модель `llama3.2:1b` (≈ 1.3 ГБ) у том `ollama_models` | перший запуск — кілька хвилин, далі — секунди |
| `server` | пошуковий сервер, порт `8080` | стартує, коли `dataset` завершився; одразу будує індекс у фоні |

Система готова, коли в лозі з'явилися рядки:

```
server-1  | listening on :8080 (...)
server-1  | index built: 107xxx docs, ... ms, N threads
```

Поки модель завантажується, `GET /api/health` повертає `"ollama": false`, а пошук працює без
розширення запиту. Web UI: <http://localhost:8080>. Зупинка: `Ctrl+C`, потім `docker compose down`
(томи з датасетом і моделлю зберігаються; `docker compose down -v` видаляє і їх).

Параметри запуску (необов'язкові, задаються змінними середовища або у файлі `.env`):

| Змінна | За замовчуванням | Призначення |
|---|---|---|
| `DATASET_SIZE_MB` | `2100` | обсяг згенерованого датасету, МБ |
| `OLLAMA_MODEL` | `llama3.2:1b` | модель Ollama |
| `HTTP_THREADS` | `32` | розмір пулу потоків для з'єднань |

GPU (NVIDIA) для Ollama: `docker compose -f docker-compose.yml -f docker-compose.gpu.yml up --build`.

## Датасет

**Формат.** Набір текстових файлів `*.txt` у кодуванні UTF-8 (англійською). Один файл — один
документ. Файли шукаються рекурсивно, інші розширення ігноруються. Ідентифікатор документа
(`doc_id`) — шлях файлу відносно `/data`, наприклад `generated/0042/doc_0042137.txt`.

**Розташування в контейнері `server`** (каталог `DATA_DIR=/data`):

| Шлях у контейнері | Джерело | Вміст |
|---|---|---|
| `/data/generated` | Docker-том `dataset` (лише читання) | згенерований датасет, створюється автоматично |
| `/data/local` | каталог `./data` репозиторію (лише читання) | власні `*.txt` файли, необов'язково |

Згенерований датасет складається з документів ≈ 20 КБ, кожен з речень на 1–2 теми (space, film,
music, food, sport, tech, nature, history, health, travel). Генератор: `scripts/gen_dataset.py`
(детермінований, `--seed 42`). Його можна запустити й вручну:
`python scripts/gen_dataset.py --out data/generated --size-mb 2100`.

**Власний датасет.** Покладіть `*.txt` файли у `./data` (наприклад,
[Large Movie Review Dataset](http://ai.stanford.edu/~amaas/data/sentiment/):
`tar xzf aclImdb_v1.tar.gz -C data`) і перебудуйте індекс (`POST /api/index`). Щоб індексувати
лише власні файли, запустіть з `DATASET_SIZE_MB=0`.

## API

Базова адреса: `http://localhost:8080`. Тіла запитів і відповідей — JSON у UTF-8
(`Content-Type: application/json`). Невідомі поля в запитах ігноруються.

### Помилки

Некоректний запит повертає статус `400` і тіло:

| Поле | Тип | Значення |
|---|---|---|
| `status` | string | завжди `"error"` |
| `message` | string | опис помилки |

```json
{"status": "error", "message": "threads must be an integer in [1, 256]"}
```

### `GET /api/health`

Стан сервера. Завжди `200`, поки сервер працює.

| Поле відповіді | Тип | Значення |
|---|---|---|
| `status` | string | завжди `"ok"` |
| `ollama` | boolean | `true`, якщо Ollama відповідає і модель `OLLAMA_MODEL` завантажена (результат кешується на 5 с) |

```json
{"status": "ok", "ollama": true}
```

### `POST /api/index`

Будує індекс з усіх `*.txt` у `/data` і публікує його. Запит синхронний: відповідь приходить
після завершення побудови. Одночасні запити на побудову виконуються по черзі; пошук під час
побудови працює зі старим індексом.

| Поле запиту | Тип | Обов'язкове | Значення |
|---|---|---|---|
| `threads` | integer | ні | кількість потоків пулу, від `1` до `256`; за замовчуванням — кількість ядер |

Тіло можна не передавати зовсім. Помилка `400`: тіло не є JSON-об'єктом; `threads` не ціле число
або поза межами `[1, 256]`.

| Поле відповіді | Тип | Значення |
|---|---|---|
| `status` | string | `"ok"` |
| `documents_indexed` | integer | кількість проіндексованих документів |
| `time_ms` | integer | час побудови, мс |

```json
// запит
{"threads": 4}
// відповідь 200
{"status": "ok", "documents_indexed": 107374, "time_ms": 18234}
```

### `POST /api/search`

Пошук за ключовими словами (BM25) без розширення запиту.

| Поле запиту | Тип | Обов'язкове | Значення |
|---|---|---|---|
| `query` | string | так | текст запиту |
| `mode` | string | ні | `"keyword"` (єдиний режим); за замовчуванням `"keyword"` |

Помилка `400`: немає `query` або це не рядок; `mode` не рядок або не `"keyword"`.

Запит розбивається на терми так само, як документи: послідовності літер і цифр (UTF-8 слова
зберігаються), ASCII-літери в нижньому регістрі, терми коротші за 2 символи та англійські
стоп-слова відкидаються.

| Поле відповіді | Тип | Значення |
|---|---|---|
| `results` | array of object | до 10 (`SEARCH_TOP_K`) документів за спаданням `score`; `[]`, якщо нічого не знайдено або індекс ще не побудовано |
| `results[].doc_id` | string | шлях документа відносно `/data` |
| `results[].score` | number | оцінка BM25 (≥ 0, округлена до 4 знаків; не нормалізована до 1) |
| `results[].snippet` | string | фрагмент документа навколо першого знайденого терма (≈ 240 символів, `...` на обрізаних краях) |
| `time_ms` | integer | час обробки запиту, мс |

```json
// запит
{"query": "space telescope", "mode": "keyword"}
// відповідь 200
{"results": [{"doc_id": "generated/0000/doc_0000754.txt", "score": 8.0184,
              "snippet": "My large said the rocket was next but the space was better..."}],
 "time_ms": 23}
```

### `GET /api/stats`

Статистика поточного індексу. До першої побудови всі значення `0`.

| Поле відповіді | Тип | Значення |
|---|---|---|
| `documents` | integer | кількість документів в індексі |
| `index_build_time_ms` | integer | час останньої побудови, мс |
| `threads_used` | integer | кількість потоків останньої побудови |

```json
{"documents": 107374, "index_build_time_ms": 18234, "threads_used": 4}
```

### WebSocket `/ws/search`

Повний пайплайн пошуку: розширення запиту через LLM → пошук BM25. Адреса:
`ws://localhost:8080/ws/search`. Обмін — текстовими кадрами з JSON. В одному з'єднанні можна
надіслати кілька запитів по черзі.

**Повідомлення клієнта:**

| Поле | Тип | Обов'язкове | Значення |
|---|---|---|---|
| `query` | string | так | текст запиту |

**Повідомлення сервера.** На кожен запит сервер надсилає рівно три повідомлення в такому порядку.
Кожне має поле `stage` (string) і, крім `done`, поле `data` (object).

1. `query_expansion` — результат розширення запиту через Ollama `/api/generate`:

   | Поле `data` | Тип | Значення |
   |---|---|---|
   | `original` | string | запит без змін |
   | `expanded_terms` | array of string | до 8 доданих LLM термів (синоніми й ключові слова, без термів самого запиту); `[]`, якщо Ollama недоступна або не відповіла за 30 с |

2. `keyword_search` — результат BM25 за термами запиту (вага 1.0) і доданими термами (вага 0.5):

   | Поле `data` | Тип | Значення |
   |---|---|---|
   | `results` | array of object | у тому самому форматі, що `results` у `POST /api/search` |
   | `time_ms` | integer | час пошуку, мс |

3. `done` — кінець обробки запиту, без `data`.

```json
{"stage": "query_expansion", "data": {"original": "good movies about space",
                                      "expanded_terms": ["galaxy", "astronaut", "rocket"]}}
{"stage": "keyword_search", "data": {"results": [{"doc_id": "...", "score": 14.018, "snippet": "..."}],
                                     "time_ms": 12}}
{"stage": "done"}
```

Якщо повідомлення клієнта не є JSON-об'єктом з рядковим `query`, сервер надсилає одне
повідомлення `error`, і з'єднання лишається відкритим:

```json
{"stage": "error", "data": {"message": "field 'query' (string) is required"}}
```

## CLI

Консольний клієнт `search` працює з запущеною системою. Адреса сервера — змінна `SEARCH_URL`
(за замовчуванням `http://localhost:8080`).

| Команда | Що робить | Ендпоінт |
|---|---|---|
| `search "запит"` | пошук за ключовими словами | `POST /api/search` |
| `search --ws "запит"` | повний пайплайн: розширені терми та результати по етапах | `/ws/search` |
| `search --index [threads]` | побудова індексу, виводить JSON-відповідь | `POST /api/index` |

Як запустити:

```bash
# Linux, macOS, Git Bash: скрипт у корені репозиторію
./search "space telescope"
./search --ws "good movies about space"
./search --index 8
```

```powershell
# Windows PowerShell (той самий клієнт усередині контейнера server)
docker compose exec server search "space telescope"
docker compose exec server search --ws "good movies about space"
docker compose exec server search --index 8
```

`./search` запускає локально зібраний `build/search`, а якщо його немає — клієнт усередині
контейнера `server` (`docker compose exec`). Приклад виводу:

```
1. generated/0000/doc_0000754.txt  (score 8.0184)
   My large said the rocket was next but the space was better...
(23 ms)
```

Код виходу: `0` — успіх, `1` — сервер недоступний або повернув помилку.

## Архітектура

```
src/
  concurrency/  ThreadPool (м'ютекс + condition_variable, FIFO-черга задач), WaitGroup
  text/         Tokenizer (терми в нижньому регістрі, стоп-слова, UTF-8)
  index/        ConcurrentHashMap (lock striping), InvertedIndex, IndexBuilder, IndexHolder
  search/       Searcher (BM25), SnippetExtractor, QueryExpander
  llm/          OllamaClient (черга запитів до LLM з окремим потоком і дедлайнами)
  api/          HttpServer, REST- і WebSocket-обробники, JSON-контракт
  cli/          клієнт командного рядка
web/            Web UI (HTML + JS, WebSocket)
scripts/        генератор датасету, дослідження масштабованості
docs/uml/       UML-діаграми (PlantUML + PNG)
```

Ключові рішення щодо паралелізму:

- **Побудова індексу.** Файли діляться на пакети по `INDEX_BATCH_DOCS` документів; кожен пакет —
  задача пулу з `threads` потоків. Воркер читає й токенізує документи у власну локальну таблицю
  (без синхронізації), потім вставляє постинги у спільний індекс. `WaitGroup` чекає на всі пакети,
  далі `Finalize` сортує списки постингів паралельно — одна задача на сегмент.
- **Потокобезпечна хеш-таблиця.** Таблиця поділена на `INDEX_SEGMENTS` сегментів (lock striping):
  у кожного свій `shared_mutex` і власна таблиця з відкритою адресацією. Запис блокує лише один
  сегмент ексклюзивно, читання — у спільному режимі. Сегменти вирівняні на 64 байти, щоб уникнути
  false sharing.
- **Публікація індексу.** Готовий індекс публікується як `shared_ptr<const InvertedIndex>`. Запит
  бере знімок і працює з ним, тож перебудова не заважає пошукам, що вже виконуються.
- **Паралельні клієнти.** cpp-httplib передає кожне з'єднання нашому `ThreadPool`
  (`PoolTaskQueue`, `HTTP_THREADS` воркерів), тому до `HTTP_THREADS` клієнтів обслуговуються одночасно.
- **LLM.** Ollama обробляє одну генерацію за раз, тому запити йдуть у обмежену чергу
  (`OLLAMA_MAX_QUEUE`), яку розбирає один потік (producer-consumer). Клієнт чекає з дедлайном
  (`OLLAMA_TIMEOUT_MS`); при тайм-ауті чи недоступності Ollama пошук продовжується без розширення.

## Налаштування сервера (змінні середовища)

| Змінна | За замовчуванням | Призначення |
|---|---|---|
| `DATA_DIR` | `/data` | каталог з `*.txt` (рекурсивно) |
| `WEB_DIR` | `/app/web` | статичні файли Web UI |
| `PORT` | `8080` | порт HTTP і WebSocket |
| `HTTP_THREADS` | `32` | розмір пулу обробки з'єднань |
| `INDEX_ON_START` | `1` | `1` — побудувати індекс одразу після старту, `0` — лише через `POST /api/index` |
| `INDEX_SEGMENTS` | `1024` | кількість сегментів хеш-таблиці |
| `INDEX_BATCH_DOCS` | `64` | документів в одній задачі пулу |
| `SEARCH_TOP_K` | `10` | кількість результатів |
| `SNIPPET_RADIUS` | `120` | символів сніпета з кожного боку від знайденого терма |
| `OLLAMA_URL` / `OLLAMA_MODEL` | `http://ollama:11434` / `llama3.2:1b` | LLM |
| `OLLAMA_TIMEOUT_MS` | `30000` | дедлайн розширення запиту |
| `OLLAMA_HEALTH_TTL_MS` | `5000` | кешування перевірки доступності Ollama |
| `OLLAMA_MAX_QUEUE` | `16` | максимальна довжина черги до LLM |
| `MAX_EXPANDED_TERMS` | `8` | максимум доданих LLM термів |
| `EXPANSION_WEIGHT` | `0.5` | вага доданих LLM термів у BM25 |

## Збирання без Docker

Потрібні g++ ≥ 11 і CMake ≥ 3.20 (залежності cpp-httplib і nlohmann/json завантажуються під час
configure).

```bash
cmake -S . -B build && cmake --build build -j
python3 scripts/gen_dataset.py --out data/generated --size-mb 100
DATA_DIR=./data WEB_DIR=./web OLLAMA_URL=http://localhost:11434 ./build/server
```

## Дослідження масштабованості

Кожен клієнт бенчмарку тримає власне keep-alive з'єднання, а кожне з'єднання займає один потік
пулу сервера. Тому для замірів з великою кількістю клієнтів сервер запускається з пулом не меншим
за `--max-clients`:

```bash
HTTP_THREADS=256 docker compose up --build        # PowerShell: $env:HTTP_THREADS=256; docker compose up --build
pip install matplotlib
python3 scripts/benchmark.py --max-threads 96 --max-clients 256
```

Скрипт міряє:

- час побудови індексу для 1, 2, 4 … N потоків (медіана з кількох запусків), прискорення S = T1/Tp
  та ефективність E = S/p; ідеальне прискорення обмежене кількістю логічних ядер: min(p, ядра);
- пропускну здатність пошуку (запитів/с) для 1, 2, 4 … M одночасних клієнтів, середню затримку на
  клієнті та середній час обробки на сервері (`time_ms`); їх різниця — час очікування в черзі та
  мережі.

Результати: `index_time.csv`, `throughput.csv`, графіки `index_time_vs_threads.png`,
`throughput_vs_clients.png` і таблиця тестових запитів `sample_queries.md` у `docs/results/`.

## Перевірка на стани гонки

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan -j
```

Під ThreadSanitizer сервер проходить одночасні побудову індексу, перебудову під час пошуку
та паралельні REST/WebSocket-запити без попереджень.