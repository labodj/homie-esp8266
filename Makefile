PYTHON ?= python3
MARKDOWNLINT ?= npx --yes markdownlint-cli2@0.24.0
PYTHON_CHECK_FILES := scripts/homie_ota.py scripts/ota_updater/ota_updater.py scripts/ota_updater/test_ota_updater.py tools/generate_config_ui.py tests/config/test_ui_asset.py
OTA_CORE_DIR ?= $(HOME)/.platformio/packages/framework-arduinoespressif32/cores/esp32
MQTT_CLIENT_DIR ?=
ARDUINOJSON_DIR ?=

config-storage-test config-reset-test: config-%-test:
	@set -eu; tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
	$(CXX) -std=c++11 -O1 -g -Wall -Wextra -Werror \
	  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	  "tests/config/test_$*.cpp" -o "$$tmp/config-test"; \
	"$$tmp/config-test"

config-connection-test:
	@test -f "$(ARDUINOJSON_DIR)/src/ArduinoJson.h" || { echo "Set ARDUINOJSON_DIR to an installed ArduinoJson 6 directory"; exit 1; }
	@set -eu; tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
	for arch in ESP32 ESP8266; do \
	  $(CXX) -std=c++11 -O1 -g -Wall -Wextra -Werror \
	    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	    -D$$arch -I "$(ARDUINOJSON_DIR)/src" tests/config/test_connections.cpp -o "$$tmp/config-test"; \
	  "$$tmp/config-test"; \
	done

config-filesystem-test:
	@set -eu; tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
	for arch in ESP32 ESP8266; do \
	for littlefs in 0 1; do \
	  $(CXX) -std=c++11 -O1 -g -Wall -Wextra -Werror \
	    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
	    -D$$arch -DHOMIE_USE_LITTLEFS=$$littlefs tests/config/test_filesystem.cpp -o "$$tmp/config-test"; \
	  "$$tmp/config-test"; \
	done; \
	done

config-ui-check:
	$(PYTHON) tools/generate_config_ui.py --check

config-test: config-ui-check config-storage-test config-reset-test config-connection-test config-filesystem-test
	node --test tests/config/test_ui.mjs

# Supply an installed espMqttClient checkout; CI installs the manifest's exact pin.
mqtt-test: mqtt-message-test
	@test -f "$(MQTT_CLIENT_DIR)/src/MqttClient.cpp" || { echo "Set MQTT_CLIENT_DIR to the installed espMqttClient directory"; exit 1; }
	@set -eu; tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
	for arch in ESP32 ESP8266; do \
	  $(CXX) -std=c++11 -O1 -g -Wall -Wextra -Werror -Wno-implicit-fallthrough \
	    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -pthread -D$$arch \
	    -I tests/mqtt -I "$(MQTT_CLIENT_DIR)/src" tests/mqtt/test_mqtt.cpp \
	    "$(MQTT_CLIENT_DIR)/src/MqttClient.cpp" "$(MQTT_CLIENT_DIR)/src/TypeDefs.cpp" \
	    "$(MQTT_CLIENT_DIR)/src/Packets/Packet.cpp" "$(MQTT_CLIENT_DIR)/src/Packets/Parser.cpp" \
	    "$(MQTT_CLIENT_DIR)/src/Packets/RemainingLength.cpp" "$(MQTT_CLIENT_DIR)/src/Packets/StringUtil.cpp" \
	    "$(MQTT_CLIENT_DIR)/src/Transport/ClientPosixIPAddress.cpp" -o "$$tmp/mqtt-test"; \
	  echo "MQTT adapter: $$arch"; "$$tmp/mqtt-test"; \
	done

# Compile the production control-ACK and payload helpers, not a copied model.
mqtt-message-test:
	@set -eu; tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
	for arch in ESP32 ESP8266; do \
	for preallocated in 0 1; do \
	  for limit in 512 4096; do \
	    $(CXX) -std=c++11 -O1 -g -Wall -Wextra -Werror \
	      -fsanitize=address,undefined -fno-sanitize-recover=all -pthread -D$$arch \
	      -DHOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED=$$preallocated \
	      -DHOMIE_PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH=$$limit \
	      tests/mqtt/test_messages.cpp -o "$$tmp/messages-test"; \
	    "$$tmp/messages-test"; \
	  done; \
	done; \
	done

# Use Arduino's actual libb64 decoder, installed by the pioarduino firmware build.
ota-test:
	@set -eu; tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
	test -f "$(OTA_CORE_DIR)/libb64/cdecode.c" || { echo "Build the ESP32 pioarduino example first, or set OTA_CORE_DIR"; exit 1; }; \
	for arch in ESP32 ESP8266; do \
	  for char_mode in signed unsigned; do \
	    $(CXX) -std=c++11 -O1 -g -Wall -Wextra -Werror -Wno-implicit-fallthrough \
	      -fsanitize=address,undefined -fno-omit-frame-pointer -pthread -f$$char_mode-char -D$$arch \
	      -I "$(OTA_CORE_DIR)" tests/ota/test_ota.cpp "$(OTA_CORE_DIR)/libb64/cdecode.c" -o "$$tmp/ota-test"; \
	    echo "OTA fault injection: $$arch, $$char_mode char"; "$$tmp/ota-test"; \
	  done; \
	done

cpplint:
	cpplint --repository=. --recursive --filter=-whitespace/line_length,-legal/copyright,-runtime/printf,-build/include,-build/namespace,-runtime/int,-whitespace/comments,-runtime/threadsafe_fn,-whitespace/indent_namespace,-runtime/references,-whitespace/newline,-whitespace/parens,-whitespace/braces ./src

python-format:
	$(PYTHON) -m ruff format $(PYTHON_CHECK_FILES)

python-format-check:
	$(PYTHON) -m ruff format --check $(PYTHON_CHECK_FILES)

python-lint:
	$(PYTHON) -m ruff check $(PYTHON_CHECK_FILES)

python-typecheck:
	$(PYTHON) -m mypy

python-test:
	$(PYTHON) -m pytest scripts/ota_updater/test_ota_updater.py tests/config/test_ui_asset.py

python-check: python-format-check python-lint python-typecheck python-test

docs-lint:
	$(MARKDOWNLINT) "docs/**/*.md" README.md

docs-fix:
	$(MARKDOWNLINT) --fix "docs/**/*.md" README.md

docs-build:
	$(PYTHON) -m mkdocs build --strict

docs-check: docs-lint docs-build

.PHONY: config-test config-ui-check config-storage-test config-reset-test config-connection-test config-filesystem-test mqtt-test mqtt-message-test ota-test cpplint python-format python-format-check python-lint python-typecheck python-test python-check docs-lint docs-fix docs-build docs-check
