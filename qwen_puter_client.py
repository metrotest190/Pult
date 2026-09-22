#!/usr/bin/env python3
"""
Улучшенная интеграция Qwen3.8-Flash через Puter.js
Для использования в Hermes Agent
"""

import requests
import json
import time
import hashlib
import logging
from typing import Optional, Dict, Any, List
from dataclasses import dataclass
from enum import Enum

# Настройка логирования
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

class TaskType(Enum):
    """Типы задач для выбора модели"""
    GENERAL = "general"
    CODING = "coding"
    CREATIVE = "creative"
    ANALYSIS = "analysis"
    TRANSLATION = "translation"
    SUMMARY = "summary"

@dataclass
class QwenResponse:
    """Структура ответа от Qwen"""
    text: str
    model: str
    tokens_used: int
    processing_time: float
    success: bool
    error: Optional[str] = None

class QwenPuterClient:
    """Улучшенный клиент для работы с Qwen через Puter.js"""
    
    def __init__(self, config: Optional[Dict] = None):
        self.config = config or self._load_default_config()
        self.cache = {}
        self.request_count = 0
        
        logger.info("QwenPuterClient инициализирован")
    
    def _load_default_config(self) -> Dict:
        """Загрузка конфигурации по умолчанию"""
        return {
            "base_url": "https://api.puter.com",
            "default_model": "gpt-3.5-turbo",  # Puter.js использует другие модели
            "timeout": 30,
            "max_retries": 3,
            "cache_enabled": True,
            "cache_ttl": 3600,
            "task_settings": {
                TaskType.GENERAL: {
                    "model": "gpt-3.5-turbo",
                    "temperature": 0.7,
                    "max_tokens": 1000
                },
                TaskType.CODING: {
                    "model": "gpt-4",
                    "temperature": 0.3,
                    "max_tokens": 2000
                },
                TaskType.CREATIVE: {
                    "model": "gpt-4",
                    "temperature": 0.8,
                    "max_tokens": 1500
                },
                TaskType.ANALYSIS: {
                    "model": "gpt-4",
                    "temperature": 0.5,
                    "max_tokens": 1200
                },
                TaskType.TRANSLATION: {
                    "model": "gpt-3.5-turbo",
                    "temperature": 0.3,
                    "max_tokens": 1000
                },
                TaskType.SUMMARY: {
                    "model": "gpt-3.5-turbo",
                    "temperature": 0.5,
                    "max_tokens": 800
                }
            }
        }
    
    def _get_cache_key(self, message: str, model: str, temperature: float) -> str:
        """Генерация ключа для кэширования"""
        data = f"{message}|{model}|{temperature}"
        return hashlib.md5(data.encode()).hexdigest()
    
    def _get_from_cache(self, cache_key: str) -> Optional[QwenResponse]:
        """Получение ответа из кэша"""
        if not self.config["cache_enabled"]:
            return None
            
        if cache_key in self.cache:
            cached_data, timestamp = self.cache[cache_key]
            if time.time() - timestamp < self.config["cache_ttl"]:
                logger.info(f"Ответ найден в кэше")
                return cached_data
            else:
                del self.cache[cache_key]
        
        return None
    
    def _save_to_cache(self, cache_key: str, response: QwenResponse):
        """Сохранение ответа в кэш"""
        if self.config["cache_enabled"]:
            self.cache[cache_key] = (response, time.time())
    
    def _make_request(self, payload: Dict) -> QwenResponse:
        """Отправка запроса к API Puter.js"""
        start_time = time.time()
        
        try:
            # Используем правильный синтаксис Puter.js
            response = self.session.post(
                "https://api.puter.com/ai/chat", 
                json=payload, 
                timeout=self.config['timeout']
            )
            response.raise_for_status()
            
            processing_time = time.time() - start_time
            
            # Парсинг ответа
            result = response.json()
            
            # Извлечение текста ответа
            text = ""
            if "text" in result:
                text = result["text"]
            elif "response" in result:
                text = result["response"]
            elif "choices" in result and len(result["choices"]) > 0:
                text = result["choices"][0]["message"]["content"]
            else:
                text = str(result)
            
            # Подсчет токенов (приблизительно)
            tokens_used = len(text.split()) + len(payload["messages"][0]["content"].split())
            
            return QwenResponse(
                text=text,
                model=payload.get("model", "unknown"),
                tokens_used=tokens_used,
                processing_time=processing_time,
                success=True
            )
            
        except requests.exceptions.RequestException as e:
            processing_time = time.time() - start_time
            logger.error(f"Ошибка запроса: {e}")
            
            return QwenResponse(
                text="",
                model=payload.get("model", "unknown"),
                tokens_used=0,
                processing_time=processing_time,
                success=False,
                error=str(e)
            )
    
    def chat(self, 
             message: str, 
             task_type: TaskType = TaskType.GENERAL,
             temperature: Optional[float] = None,
             max_tokens: Optional[int] = None,
             use_cache: bool = True) -> QwenResponse:
        """
        Отправка запроса к Qwen модели
        
        Args:
            message: Сообщение пользователя
            task_type: Тип задачи для выбора оптимальной модели
            temperature: Температура генерации
            max_tokens: Максимальное количество токенов
            use_cache: Использовать кэширование
            
        Returns:
            Ответ модели
        """
        # Получение настроек для типа задачи
        task_settings = self.config["task_settings"].get(task_type, {})
        
        # Определение модели
        model = task_settings.get("model", self.config["default_model"])
        
        # Определение температуры
        if temperature is None:
            temperature = task_settings.get("temperature", 0.7)
        
        # Определение максимальных токенов
        if max_tokens is None:
            max_tokens = task_settings.get("max_tokens", 1000)
        
        # Проверка кэша
        if use_cache:
            cache_key = self._get_cache_key(message, model, temperature)
            cached_response = self._get_from_cache(cache_key)
            if cached_response:
                return cached_response
        
        # Формирование payload
        payload = {
            "model": model,
            "messages": [{"role": "user", "content": message}],
            "temperature": temperature,
            "max_tokens": max_tokens,
            "stream": False
        }
        
        # Отправка запроса
        response = self._make_request(payload)
        
        # Сохранение в кэш
        if use_cache and response.success:
            self._save_to_cache(cache_key, response)
        
        self.request_count += 1
        logger.info(f"Запрос #{self.request_count} к модели {model}")
        
        return response
    
    def chat_stream(self, 
                    message: str, 
                    task_type: TaskType = TaskType.GENERAL,
                    temperature: Optional[float] = None,
                    max_tokens: Optional[int] = None) -> str:
        """
        Потоковая генерация ответа
        
        Args:
            message: Сообщение пользователя
            task_type: Тип задачи
            temperature: Температура генерации
            max_tokens: Максимальное количество токенов
            
        Returns:
            Ответ модели (полный)
        """
        # Получение настроек для типа задачи
        task_settings = self.config["task_settings"].get(task_type, {})
        
        # Определение модели
        model = task_settings.get("model", self.config["default_model"])
        
        # Определение температуры
        if temperature is None:
            temperature = task_settings.get("temperature", 0.7)
        
        # Определение максимальных токенов
        if max_tokens is None:
            max_tokens = task_settings.get("max_tokens", 1000)
        
        # Формирование payload для Puter.js
        payload = {
            "messages": [{"role": "user", "content": message}],
            "temperature": temperature,
            "max_tokens": max_tokens,
            "stream": True
        }
        
        full_response = ""
        
        try:
            response = self.session.post(
                "https://api.puter.com/ai/chat", 
                json=payload, 
                timeout=60
            )
            response.raise_for_status()
            
            # Puter.js не поддерживает потоковый режим, поэтому просто читаем ответ
            result = response.json()
            
            if "text" in result:
                full_response = result["text"]
            elif "response" in result:
                full_response = result["response"]
            elif "choices" in result and len(result["choices"]) > 0:
                full_response = result["choices"][0]["message"]["content"]
            else:
                full_response = str(result)
                
            print(full_response)
            return full_response
            
        except requests.exceptions.RequestException as e:
            logger.error(f"Ошибка потокового запроса: {e}")
            return f"Ошибка: {e}"
    
    def get_available_models(self) -> List[str]:
        """Получение списка доступных моделей"""
        try:
            # Puter.js использует другие модели, мы можем их получить через API
            response = self.session.get(
                "https://api.puter.com/ai/listModels", 
                timeout=10
            )
            response.raise_for_status()
            
            result = response.json()
            models = []
            
            # Извлекаем доступные модели
            if "models" in result:
                for model_info in result["models"]:
                    if "name" in model_info:
                        models.append(model_info["name"])
            
            return models
            
        except requests.exceptions.RequestException as e:
            logger.error(f"Ошибка получения моделей: {e}")
            return []
    
    def get_stats(self) -> Dict:
        """Получение статистики использования"""
        return {
            "total_requests": self.request_count,
            "cache_size": len(self.cache),
            "cache_enabled": self.config["cache_enabled"],
            "available_models": len(self.get_available_models())
        }

# Примеры использования
def main():
    """Тестирование клиента"""
    print("🚀 Тестирование QwenPuterClient...")
    
    client = QwenPuterClient()
    
    # Тест 1: Общий запрос
    print("\n1. 📝 Общий запрос:")
    response = client.chat("Привет! Расскажи о себе одним предложением.")
    print(f"✅ Ответ: {response.text}")
    print(f"📊 Статистика: {response}")
    
    # Тест 2: Кодирование
    print("\n2. 💻 Кодирование:")
    response = client.chat("Напиши функцию Python для вычисления факториала", TaskType.CODING)
    print(f"✅ Ответ: {response.text[:200]}...")
    print(f"📊 Статистика: {response}")
    
    # Тест 3: Творческий запрос
    print("\n3. 🎨 Творческий запрос:")
    response = client.chat("Придумай короткую историю о роботе", TaskType.CREATIVE)
    print(f"✅ Ответ: {response.text[:200]}...")
    print(f"📊 Статистика: {response}")
    
    # Статистика
    print("\n📈 Общая статистика:")
    stats = client.get_stats()
    print(f"Всего запросов: {stats['total_requests']}")
    print(f"Размер кэша: {stats['cache_size']}")
    print(f"Доступных моделей: {stats['available_models']}")

if __name__ == "__main__":
    main()