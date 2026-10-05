# KenshiCoop — наш форк (protocol 56)

Форк открытого кооп-мода [KenshiCoop](https://github.com/nhoral/KenshiCoop) (nhoral, AGPL-3.0)
для Kenshi 1.0.65 / 1.0.68. Добавляет отдельное меню **МУЛЬТИПЛЕЕР** в главном меню игры.

## Что добавлено

- **Меню MULTIPLAYER** — клавиша **F10** в главном меню Kenshi:
  - **STEAM** — браузер публичных серверов (имя, игроки 1/2, замок при пароле),
    кнопки Refresh и «Host a server»;
  - **LAN** — IP, порт, пароль, имя сервера и кнопки HOST / CONNECT.
- **Пароль сервера** проверяется в рукопожатии: хэш в HELLO, при несовпадении хост
  отвечает AUTH_FAIL и отключает (пустой пароль = открытый сервер).
- LAN-значения сохраняются в `coop_config.json` и применяются без перезапуска игры.
- Старая панель F2 и ядро синхронизации не изменялись.

## Совместимость

- Сетевой протокол **56**. Соединяются только одинаковые билды: официальный KenshiCoop
  v0.51 (protocol 55) **не соединится** — так задумано.
- Оба игрока должны стоять на этой версии.

## Установка (игрокам)

1. Скачайте `Kenshi-Co-op-portable.zip` из [Releases](../../releases/latest).
2. Распакуйте и запустите `KenshiCoop Launcher.exe` → «Установить» → «Играть».
3. В главном меню игры нажмите **F10** → MULTIPLAYER.

Лаунчер сам получает обновления этого репозитория (GitHub-релизы).

## Сборка

- Плагин: `scripts\build_plugin_ours.cmd Release` (портативный VC++2010);
  зависимости — `third_party\` + `scripts\patch_deps.py`.
- Упаковка релиза: `tools\publish_release.py` (кит + `channel.json`).

## Лицензия и благодарности

- **AGPL-3.0** (см. `LICENSE`) — как у апстрима; исходники форка лежат в этом
  репозитории, бинарники — в Releases (соответствие требованиям AGPL).
- Оригинал: [nhoral/KenshiCoop](https://github.com/nhoral/KenshiCoop);
  загрузчик плагинов: [BFrizzleFoShizzle/RE_Kenshi](https://github.com/BFrizzleFoShizzle/RE_Kenshi).
- Собрано с использованием ИИ (AI-assisted), 2026.
