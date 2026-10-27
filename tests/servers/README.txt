Серверы Minecraft для тестов tMC.

  mc26\server.jar   — сервер 26.3 (native-формат миров)
  mc121\server.jar  — сервер 1.21.11 (legacy-формат)

Скрипты (tools\server_check.py, tools\smoke_mc.py, tools\run_mc_server.py, tools\run_mc121_server.py) сами пишут eula.txt и
server.properties, распаковывают мир в подпапку сервера и после первого запуска создают рядом libraries/, versions/, логи.
Эти файлы и jar в репозиторий не коммитятся (см. tests\.gitignore). На новом компьютере положите server.jar нужных версий
с minecraft.net в mc26 и mc121.
Java берётся из переменной JAVA, затем из tests\local_config.json, затем из PATH.
