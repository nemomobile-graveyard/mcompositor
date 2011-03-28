shell_scripts.commands += ./gen-tests-xml.sh > tests.xml
shell_scripts.files += tests.xml
shell_scripts.CONFIG += no_check_exist

shell_scripts.path += /usr/share/mcompositor-tests/
shell_scripts.depends = FORCE

INSTALLS    += \
              shell_scripts
