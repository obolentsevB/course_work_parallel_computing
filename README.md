# Паралельна пошукова система з інтеграцією LLM

Курсова робота з дисципліни «Паралельні обчислення» (КПІ, ІПСА).

Пошуковий сервер на C++20: паралельна побудова інвертованого індексу власним пулом потоків,
власна потокобезпечна хеш-таблиця, паралельна обробка клієнтів, розширення запиту
через локальну LLM (Ollama), REST API і WebSocket-стрімінг етапів пошуку.

## Швидкий старт

```bash
# 1. Датасет (≥ 2 ГБ, ≥ 10 000 документів) у ./data
python3 scripts/gen_dataset.py --out data/generated --size-mb 2100

# 2. Запуск усієї системи
docker compose up --build

# 3. Перевірка та побудова індексу
curl localhost:8080/api/health
./search --index 8            # або: curl -XPOST localhost:8080/api/index -d '{"threads": 8}'

# 4. Пошук
./search "space telescope"                    # REST, режим keyword
./search --ws "які хороші фільми про космос?" # WebSocket: LLM-розширення + пошук
```

Web UI: <http://localhost:8080>.

Перший запуск завантажує модель (`llama3.2:1b`, ~1.3 ГБ) у том `ollama_models`; поки вона
вантажиться, `/api/health` повертає `"ollama": false`, а пошук працює без розширення.
Іншу модель задає змінна `OLLAMA_MODEL` (наприклад, `OLLAMA_MODEL=qwen2.5:1.5b docker compose up`).
GPU: `docker compose -f docker-compose.yml -f docker-compose.gpu.yml up`.

Замість згенерованого датасету можна покласти у `./data` будь-які `*.txt` файли,
наприклад [Large Movie Review Dataset](http://ai.stanford.edu/~amaas/data/sentiment/)
(`tar xzf aclImdb_v1.tar.gz -C data`). Файли шукаються рекурсивно.

## REST API

| Ендпоінт | Запит | Відповідь |
|---|---|---|
| `GET /api/health` | — | `{"status": "ok", "ollama": true}` |
| `POST /api/index` | `{"threads": 4}` | `{"status": "ok", "documents_indexed": 2000, "time_ms": 1234}` |
| `POST /api/search` | `{"query": "text", "mode": "keyword"}` | `{"results": [{"doc_id", "score", "snippet"}], "time_ms": 56}` |
| `GET /api/stats` | — | `{"documents": 2000, "index_build_time_ms": 1234, "threads_used": 4}` |

Некоректні параметри → `400 {"status": "error", "message": "..."}`.

**WebSocket `/ws/search`**: клієнт надсилає `{"query": "..."}`, сервер відповідає трьома повідомленнями:

```json
{"stage": "query_expansion", "data": {"original": "...", "expanded_terms": ["..."]}}
{"stage": "keyword_search", "data": {"results": [...], "time_ms": 12}}
{"stage": "done"}
```

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

## Збирання без Docker

Потрібні g++ ≥ 11 і CMake ≥ 3.20 (залежності cpp-httplib і nlohmann/json завантажуються під час configure).

```bash
cmake -S . -B build && cmake --build build -j
DATA_DIR=./data WEB_DIR=./web OLLAMA_URL=http://localhost:11434 ./build/server
```

## Дослідження масштабованості

```bash
pip install matplotlib
python3 scripts/benchmark.py --max-threads 16 --max-clients 32
```

Скрипт міряє час побудови індексу для 1, 2, 4 … N потоків (медіана з кількох запусків, прискорення
та ефективність) і пропускну здатність пошуку для 1 … M одночасних клієнтів. Результати: CSV, графіки
PNG і таблиця тестових запитів у `docs/results/`.

## Налаштування (змінні середовища)

| Змінна | За замовчуванням | Призначення |
|---|---|---|
| `DATA_DIR` | `/data` | каталог з `*.txt` |
| `HTTP_THREADS` | `32` | розмір пулу обробки з'єднань |
| `INDEX_SEGMENTS` | `1024` | кількість сегментів хеш-таблиці |
| `INDEX_BATCH_DOCS` | `64` | документів в одній задачі пулу |
| `OLLAMA_URL` / `OLLAMA_MODEL` | `http://ollama:11434` / `llama3.2:1b` | LLM |
| `OLLAMA_TIMEOUT_MS` | `30000` | дедлайн розширення запиту |
| `OLLAMA_MAX_QUEUE` | `16` | максимальна довжина черги до LLM |
| `EXPANSION_WEIGHT` | `0.5` | вага доданих LLM термів у BM25 |
| `SEARCH_TOP_K` | `10` | кількість результатів |

## Перевірка на стани гонки

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan -j
```

Під ThreadSanitizer сервер проходить одночасні побудову індексу, перебудову під час пошуку
та паралельні REST/WebSocket-запити без попереджень.