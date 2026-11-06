# daswr
# json (C)

Минимальный JSON-парсер на C11 — **один файл**, без зависимостей.

## Сборка

```sh
cc -std=c11 -O2 -Wall -Wextra json.c -o json
```

## Использование

```sh
./json file.json      # прочитать файл и напечатать с отступами
./json                # читать со stdin
```

Пример:

```sh
$ echo '{"name":"Ada","langs":["C","asm"],"ok":true}' | ./json
{
  "name": "Ada",
  "langs": [
    "C",
    "asm"
  ],
  "ok": true
}
```

## Что умеет

- `null`, `true`, `false`, числа (`int`/`double`), строки, массивы, объекты
- Экранирование: `\" \\ \/ \b \f \n \r \t \uXXXX`
- Защита от слишком глубокой вложенности (`MAX_DEPTH = 512`)
- Pretty-printer

## Ограничения

- Surrogate pairs (`\uD83D\uDE00`) не склеиваются — каждый `\u` даёт отдельный
  кодпоинт в UTF-8
- Нет API для изменения дерева, только парсинг + печать + `json_free`

## Как использовать как библиотеку

```c
#include "json.c"   /* или скомпилировать вместе с json.c и объявить extern */

const char *err;
json_value *v = json_parse("{\"x\": 42}", &err);
if (v) {
    /* обход дерева: v->type, v->as.object.pairs, v->as.array.items, ... */
    json_free(v);
}
```

## Лицензия

MIT — см. `LICENSE`.
