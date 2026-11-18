# FileWatcher — NativeAPI AddIn для 1С

Кроссплатформенная внешняя компонента (Windows x86/x64, Linux x64), которая следит за каталогом и отправляет **внешние события** в клиентский сеанс 1С при создании, изменении, удалении и переименовании файлов и папок.

## Возможности

- Локальные папки; на Windows — также UNC/SMB (`\\server\share\...`)
- События: `created`, `deleted`, `modified`, `renamed`
- Для rename передаются `oldPath` и `newPath`
- `itemType`: `file` или `directory` (для каталогов `content` пустой)
- Текстовое содержимое файла (UTF-8, на Windows при необходимости CP1251)
- Debounce для серий `modified` (~100 ms)

## Ограничения

- Имя процесса **не** определяется
- Нет macOS
- Linux: inotify надёжен на локальных FS; на NFS/CIFS события часто отсутствуют
- Windows SMB: возможны задержки и пропуски при обрыве сети
- Нет фильтра по маске файла (v1)
- Только клиентский контекст 1С (пути на машине пользователя)

## Сборка

### Windows (только MSVC — обязательно)

Компонента должна собираться **MSVC** (ABI совместим с 1С). Сборка MinGW/Clang к 1С обычно не подключается.

```bat
cmake -S . -B build-msvc64 -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc64 --config Release

cmake -S . -B build-msvc32 -G "Visual Studio 17 2022" -A Win32
cmake --build build-msvc32 --config Release
```

Артефакты: `build-msvc64\bin\Release\FileWatcher.dll` → в ZIP как `FileWatcher64.dll`,
`build-msvc32\bin\Release\FileWatcher.dll` → как `FileWatcher.dll`.

Сборки MSVC используют статическую библиотеку времени выполнения. Готовый ZIP из релиза содержит обе DLL для Windows x86 и x64 и манифест для 1С. Для Linux доступна сборка из исходников.

### Linux

```bash
sudo apt-get install build-essential cmake uuid-dev
cmake -S . -B build
cmake --build build --config Release
```

Артефакт: `build/bin/FileWatcher.so`. Скрипт: [`packaging/build-linux.sh`](packaging/build-linux.sh).

ZIP для макета 1С: `MANIFEST.XML` + бинарники платформ из `packaging/MANIFEST.XML`.

## Подключение в 1С

1. Упакуйте `FileWatcher.dll` / `FileWatcher.so` в ZIP-макет внешней компоненты (или общий макет с манифестом NativeAPI).
2. На клиенте:

```bsl
ПодключитьВнешнююКомпоненту(Местоположение, "FW", ТипВнешнейКомпоненты.Native);
Компонента = Новый("AddIn.FW.FileWatcher");
```

См. также [`examples/Demo.bsl`](examples/Demo.bsl).

### Методы (EN / RU)

| EN | RU | Описание |
|----|----|----------|
| `StartWatch(Path, Recursive = True, MaxContentBytes = 1048576)` | `НачатьСлежение` | Добавить папку в слежение (можно вызывать многократно). Булево |
| `StopWatch(Path)` | `ОстановитьСлежение` | Остановить одну папку. Булево |
| `StopAll()` | `ОстановитьВсе` | Остановить все папки |
| `Version()` | `Версия` | `1.3.5` |
| `IsWatching(Path)` | `СлежениеАктивно` | Булево |
| `GetLastError(Path)` | `ПолучитьПоследнююОшибку` | Текст ошибки по папке |
| `GetMaxContentBytes(Path)` | `ПолучитьМаксБайтСодержимого` | Лимит чтения содержимого |
| `SetMaxContentBytes(Path, Value)` | `УстановитьМаксБайтСодержимого` | Сменить лимит без перезапуска. Булево |
| `GetIncludeSubdirectories(Path)` | `ПолучитьВключаяПодкаталоги` | Была ли включена рекурсия при старте |
| `GetWatchList()` | `ПолучитьСписокПапок` | Массив строк — пути зарегистрированных папок |

Свойств нет — параметры папки задаются в `НачатьСлежение` / `StartWatch` и читаются методами выше.

```bsl
Компонента.НачатьСлежение("C:\Inbox", Истина, 1048576);
Компонента.НачатьСлежение("C:\Outbox", Ложь);
Если Не Компонента.СлежениеАктивно("C:\Inbox") Тогда
    Сообщить(Компонента.ПолучитьПоследнююОшибку("C:\Inbox"));
КонецЕсли;
```

### Внешнее событие

| Параметр | Значение |
|----------|----------|
| `Источник` | всегда `FileWatcher` |
| `Событие` | путь папки слежения (как в `НачатьСлежение`) |
| `Данные` | JSON UTF-8 (поля ниже) |

```json
{
  "type": "created|deleted|modified|renamed",
  "itemType": "file|directory",
  "path": "C:\\data\\a.txt",
  "oldPath": "",
  "newPath": "",
  "date": "2026-07-27T21:00:00",
  "content": "...",
  "contentTruncated": false,
  "contentSkipped": false
}
```

| Поле | Тип | Описание |
|------|-----|----------|
| `type` | Строка | `created` / `deleted` / `modified` / `renamed` |
| `itemType` | Строка | `file` или `directory` |
| `path` | Строка | полный путь к объекту (для `renamed` = `newPath`) |
| `oldPath` | Строка | прежний путь (только `renamed`, иначе `""`) |
| `newPath` | Строка | новый путь (только `renamed`, иначе `""`) |
| `date` | Строка | `yyyy-MM-ddTHH:mm:ss` (mtime или время события) |
| `content` | Строка | текст файла; для каталога/удаления/бинаря — `""` |
| `contentTruncated` | Булево | файл обрезан по лимиту |
| `contentSkipped` | Булево | содержимое не читалось |

## Структура репозитория

```
include/          NativeAPI-заголовки
src/              компонента, watcher (Win/Linux), утилиты
examples/Demo.bsl пример для 1С
CMakeLists.txt
```
