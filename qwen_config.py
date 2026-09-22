# Qwen3.8-Flash конфигурация через Puter.js
# Бесплатный доступ без API Key

# Настройки подключения к Qwen через Puter.js
QWEN_PUTER_BASE_URL = "https://api.puter.com"
QWEN_PUTER_MODEL = "qwen/qwen3.8-flash"

# Доступные модели
QWEN_MODELS = [
    "qwen/qwen3.8-flash",    # Быстрая модель (рекомендуемая)
    "qwen/qwen3.8-max",      # Максимальная модель
    "qwen/qwen3.7-plus",     # Универсальная модель
    "qwen/qwen3.7-max",      # Максимальная V3.7
    "qwen/qwen3.7-flash",    # Быстрая V3.7
    "qwen/qwen3.6-27b",      # Модель для кодирования
    "qwen/qwen3.6-plus",     # Универсальная V3.6
    "qwen/qwen3.6-flash",    # Быстрая V3.6
]

# Настройки по умолчанию
DEFAULT_TEMPERATURE = 0.7
DEFAULT_MAX_TOKENS = 1000
DEFAULT_TIMEOUT = 30

# Заголовки для запросов
DEFAULT_HEADERS = {
    "Content-Type": "application/json",
    "User-Agent": "Hermes-Qwen-Puter-API/1.0"
}

# Настройки для разных типов задач
TASK_SETTINGS = {
    "general": {
        "model": "qwen/qwen3.8-flash",
        "temperature": 0.7,
        "max_tokens": 1000
    },
    "coding": {
        "model": "qwen/qwen3.6-27b",
        "temperature": 0.3,
        "max_tokens": 2000
    },
    "creative": {
        "model": "qwen/qwen3.8-max",
        "temperature": 0.8,
        "max_tokens": 1500
    },
    "analysis": {
        "model": "qwen/qwen3.7-plus",
        "temperature": 0.5,
        "max_tokens": 1200
    }
}

# Кэширование для повторяющихся запросов
ENABLE_CACHING = True
CACHE_TTL = 3600  # 1 час

# Логирование
LOG_REQUESTS = True
LOG_RESPONSES = True
LOG_LEVEL = "INFO"

# Альтернативные модели для резервирования
BACKUP_MODELS = [
    "qwen/qwen3.7-plus",
    "qwen/qwen3.6-27b"
]