# Qwen3.8-Flash - Бесплатный API Integration

## 🚀 Введение

Этот проект предоставляет бесплатный доступ к моделям Qwen через различных провайдеров. Идеально подходит для интеграции с Hermes Agent и других приложений.

## ✨ Особенности

- 🆓 **Бесплатный доступ** - разные провайдеры предлагают разные условия
- 🔧 **Универсальный клиент** - поддержка множества провайдеров
- 🚀 **Высокая производительность** - быстрые ответы
- 💾 **Кэширование** - автоматическое кэширование запросов
- 🎯 **Оптимизация под задачи** - разные модели для разных типов задач
- 📊 **Статистика** - отслеживание использования

## 📁 Структура проекта

```
Pult_Encod_Koda/
├── qwen_test.html              # Веб-интерфейс для тестирования
├── qwen_puter_api.py          # Базовый API клиент для Puter.js
├── qwen_openrouter_client.py   # Клиент для OpenRouter
├── qwen_universal_client.py   # Универсальный клиент для всех провайдеров
├── qwen_config.py             # Конфигурация
└── README_Qwen.md             # Подробная документация
```

## 🛠️ Установка и использование

### 1. Веб-интерфейс (qwen_test.html)

Откройте файл `qwen_test.html` в браузере:

```bash
# Откройте в браузере
file:///C:/Users/190/Desktop/new/workspace/Pult_Encod_Koda/qwen_test.html
```

**Функции:**
- Выбор модели из выпадающего списка
- Примеры запросов
- Интерактивный чат
- Отображение статистики
- Тестовый режим без использования кредитов

### 2. Универсальный клиент (qwen_universal_client.py)

```python
from qwen_universal_client import QwenUniversalClient, Provider

# Инициализация с Puter.js (не требует API Key)
api = QwenUniversalClient(Provider.PUTER)

# Отправка запроса
response = api.chat("Привет! Расскажи о себе")
if response.success:
    print(f"Ответ: {response.text}")
    print(f"Модель: {response.model}")
    print(f"Время: {response.processing_time:.2f}s")

# Инициализация с OpenRouter (требует API Key)
api = QwenUniversalClient(Provider.OPENROUTER, "your_api_key")

# Инициализация с DeepSeek (требует API Key)
api = QwenUniversalClient(Provider.DEEPSEEK, "your_api_key")
```

### 3. OpenRouter клиент (qwen_openrouter_client.py)

```python
from qwen_openrouter_client import QwenOpenRouterClient

# Инициализация
api = QwenOpenRouterClient("your_openrouter_api_key")

# Отправка запроса
response = api.chat("Напиши функцию Python", "qwen/qwen3.8-flash")
print(response.text)

# Получение списка моделей
models = api.get_available_models()
print(f"Доступные модели: {models}")
```

## 🎯 Провайдеры и их условия

### 1. Puter.js (Рекомендуемый)
- **API Key**: Не требуется
- **Стоимость**: Бесплатно
- **Ограничения**: Тестовый режим
- **Модели**: GPT-3.5, GPT-4, Claude, DeepSeek, Gemini
- **Качество**: Отличное
- **Рекомендация**: ✅ Для быстрого старта

### 2. OpenRouter
- **API Key**: Требуется
- **Стоимость**: Бесплатный доступ к Qwen
- **Ограничения**: Нет ограничений
- **Модели**: Все модели Qwen
- **Качество**: Отличное
- **Рекомендация**: ✅ Для продвинутого использования

### 3. DeepSeek
- **API Key**: Требуется
- **Стоимость**: 5 млн бесплатных токенов
- **Ограничения**: 30 дней
- **Модели**: DeepSeek-V4-Flash, DeepSeek-V4-Pro, DeepSeek-R1
- **Качество**: Очень высокое
- **Рекомендация**: ✅ Для серьезных задач

### 4. HuggingFace
- **API Key**: Требуется
- **Стоимость**: Бесплатные модели
- **Ограничения**: Зависит от модели
- **Модели**: Qwen3.8-Flash, Qwen3.7-Plus, Qwen3.6-27b
- **Качество**: Хорошее
- **Рекомендация**: ✅ Для локальных моделей

### 5. Alibaba Cloud
- **API Key**: Требуется
- **Стоимость**: 90 дней бесплатно
- **Ограничения**: 90 дней
- **Модели**: Все модели Qwen
- **Качество**: Отличное
- **Рекомендация**: ✅ для долгосрочного использования

## 📊 Доступные модели Qwen

| Провайдер | Модель | Описание | Задачи |
|----------|--------|----------|--------|
| **OpenRouter** | `qwen/qwen3.8-flash` | Быстрая модель | Общие задачи |
| **OpenRouter** | `qwen/qwen3.8-max` | Максимальная модель | Творчество |
| **OpenRouter** | `qwen/qwen3.7-plus` | Универсальная модель | Анализ |
| **OpenRouter** | `qwen/qwen3.6-27b` | Модель для кода | Программирование |
| **DeepSeek** | `deepseek-v4-flash` | Быстрая модель | Общие задачи |
| **DeepSeek** | `deepseek-v4-pro` | Профессиональная модель | Сложные задачи |
| **HuggingFace** | `qwen/qwen3.8-flash` | Быстрая модель | Общие задачи |
| **HuggingFace** | `qwen/qwen3.7-plus` | Универсальная модель | Разные задачи |

## 🔧 Конфигурация

Файл `qwen_config.py` содержит настройки:

```python
# Выбор провайдера и модели
DEFAULT_PROVIDER = Provider.PUTER

# Настройки для разных типов задач
TASK_SETTINGS = {
    "coding": {
        "model": "qwen/qwen3.6-27b",
        "temperature": 0.3,
        "max_tokens": 2000
    },
    "creative": {
        "model": "qwen/qwen3.8-max",
        "temperature": 0.8,
        "max_tokens": 1500
    }
}
```

## 📊 Интеграция с Hermes Agent

```python
# В вашем skill или скрипте
from qwen_universal_client import QwenUniversalClient, Provider, TaskType

def qwen_chat(message, task_type=TaskType.GENERAL):
    # Используем Puter.js (бесплатно)
    client = QwenUniversalClient(Provider.PUTER)
    response = client.chat(message, temperature=0.7)
    return response.text

# Пример использования в skill
def analyze_code(code):
    prompt = f"Проанализируй этот код:\n{code}"
    return qwen_chat(prompt, TaskType.ANALYSIS)
```

## 🎮 Примеры использования

### 1. Кодирование
```python
client = QwenUniversalClient(Provider.OPENROUTER, "your_api_key")
response = client.chat("Напиши функцию для быстрой сортировки", "qwen/qwen3.6-27b")
```

### 2. Творчество
```python
client = QwenUniversalClient(Provider.PUTER)
response = client.chat("Придумай короткий рассказ о космосе")
```

### 3. Анализ
```python
client = QwenUniversalClient(Provider.DEEPSEEK, "your_api_key")
response = client.chat("Проанализируй преимущества и недостатки Python")
```

### 4. Перевод
```python
client = QwenUniversalClient(Provider.OPENROUTER, "your_api_key")
response = client.chat("Переведи на английский: Искусственный интеллект")
```

## 🔍 Отладка и логирование

Включенное логирование:

```python
import logging
logging.basicConfig(level=logging.INFO)

# Теперь все запросы будут логироваться
client = QwenUniversalClient(Provider.PUTER)
response = client.chat("Тестовый запрос")
```

## 🚨 Ограничения

- **Puter.js**: Тестовый режим, может быть медленнее
- **OpenRouter**: Требует API Key
- **DeepSeek**: 5 млн токенов на 30 дней
- **HuggingFace**: Зависит от модели
- **Alibaba**: 90 дней бесплатно

## 🔄 Альтернативные варианты

Если один провайдер недоступен, можно использовать другой:

```python
# Попробовать разные провайдers
providers = [
    (Provider.PUTER, None),
    (Provider.OPENROUTER, "your_api_key"),
    (Provider.DEEPSEEK, "your_api_key"),
]

for provider, api_key in providers:
    try:
        client = QwenUniversalClient(provider, api_key)
        response = client.chat("Тестовый запрос")
        if response.success:
            print(f"✅ {provider.value} работает")
            break
    except Exception as e:
        print(f"❌ {provider.value} не работает: {e}")
```

## 📈 Мониторинг

Отслеживайте статистику использования:

```python
stats = client.get_stats()
print(f"Провайдер: {stats['provider']}")
print(f"Всего запросов: {stats['total_requests']}")
print(f"Размер кэша: {stats['cache_size']}")
print(f"Доступных моделей: {stats['available_models']}")
```

## 🎯 Рекомендации по выбору провайдера

### Для начинающих:
1. **Puter.js** - бесплатно, без API Key
2. **OpenRouter** - бесплатно, с API Key
3. **DeepSeek** - 5 млн токенов бесплатно

### для профессионалов:
1. **OpenRouter** - все модели Qwen
2. **DeepSeek** - высокое качество
3. **Alibaba** - 90 дней бесплатно

### для кодирования:
1. **OpenRouter** - `qwen/qwen3.6-27b`
2. **DeepSeek** - `deepseek-v4-flash`
3. **HuggingFace** - `qwen/qwen3.6-27b`

## 🤝 Вклад в проект

Если вы хотите улучшить этот проект:

1. Форкните репозиторий
2. Создайте ветку для вашей функции
3. Внесите изменения
4. Создайте Pull Request

## 📄 Лицензия

MIT License - свободное использование и модификация.

## 🆘 Поддержка

При возникновении проблем:

1. Проверьте интернет-соединение
2. Убедитесь, что выбранный провайдер доступен
3. Проверьте логи на ошибки
4. Попробуйте другого провайдера
5. Создайте issue в репозитории

---

**Готово!** Теперь у вас есть полный доступ к Qwen3.8-Flash через разных провайдеров. 🎉

### Быстрый старт:

1. Откройте `qwen_test.html` в браузере для быстрого тестирования
2. Используйте `qwen_universal_client.py` для программной интеграции
3. Выберите подходящего провайдера в зависимости от ваших нужд
4. Наслаждайтесь бесплатным доступом к Qwen!