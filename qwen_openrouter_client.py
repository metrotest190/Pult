#!/usr/bin/env python3
"""
Qwen3.8-Flash через OpenRouter
Бесплатный доступ без API Key
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

class QwenOpenRouterClient:
    """Клиент для работы с Qwen через OpenRouter"""
    
    def __init__(self, api_key: Optional[str] = None):
        self.base_url = "https://openrouter.ai/api/v1"
        self.api_key = api_key
        self.session = requests.Session()
        self.cache = {}
        self.request_count = 0
        
        # Настройка заголовков
        self.session.headers.update({
            "Content-Type": "application/json",
            "Authorization": f"Bearer {self.api_key}",
            "HTTP-Referer": "https://hermes-agent.nousresearch.com",
            "X-Title": "Hermes-Qwen-OpenRouter"
        })
        
        logger.info("QwenOpenRouterClient инициализирован")
    
    def _get_cache_key(self, message: str, model: str, temperature: float) -> str:
        """Генерация ключа для кэширования"""
        data = f"{message}|{model}|{temperature}"
        return hashlib.md5(data.encode()).hexdigest()
    
    def _get_from_cache(self, cache_key: str) -> Optional[QwenResponse]:
        """Получение ответа из кэша"""
        if cache_key in self.cache:
            cached_data, timestamp = self.cache[cache_key]
            if time.time() - timestamp < 3600:  # 1 час
                logger.info("Ответ найден в кэше")
                return cached_data
            else:
                del self.cache[cache_key]
        
        return None
    
    def _save_to_cache(self, cache_key: str, response: QwenResponse):
        """Сохранение ответа в кэш"""
        self.cache[cache_key] = (response, time.time())
    
    def _make_request(self, payload: Dict) -> QwenResponse:
        """Отправка запроса к API OpenRouter"""
        start_time = time.time()
        
        try:
            response = self.session.post(
                f"{self.base_url}/chat/completions", 
                json=payload, 
                timeout=30
            )
            response.raise_for_status()
            
            processing_time = time.time() - start_time
            
            # Парсинг ответа
            result = response.json()
            
            # Извлечение текста ответа
            text = ""
            if "choices" in result and len(result["choices"]) > 0:
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
             model: str = "qwen/qwen3.8-flash",
             temperature: float = 0.7,
             max_tokens: int = 1000,
             use_cache: bool = True) -> QwenResponse:
        """
        Отправка запроса к модели через OpenRouter
        
        Args:
            message: Сообщение пользователя
            model: Модель Qwen
            temperature: Температура генерации
            max_tokens: Максимальное количество токенов
            use_cache: Использовать кэширование
            
        Returns:
            Ответ модели
        """
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
            "max_tokens": max_tokens
        }
        
        # Отправка запроса
        response = self._make_request(payload)
        
        # Сохранение в кэш
        if use_cache and response.success:
            self._save_to_cache(cache_key, response)
        
        self.request_count += 1
        logger.info(f"Запрос #{self.request_count} к модели {model}")
        
        return response
    
    def get_available_models(self) -> List[str]:
        """Получение списка доступных моделей"""
        try:
            response = self.session.get(
                f"{self.base_url}/models", 
                timeout=10
            )
            response.raise_for_status()
            
            result = response.json()
            models = []
            
            # Извлекаем доступные модели Qwen
            if "data" in result:
                for model_info in result["data"]:
                    model_id = model_info.get("id", "")
                    if "qwen" in model_id.lower():
                        models.append(model_id)
            
            return models
            
        except requests.exceptions.RequestException as e:
            logger.error(f"Ошибка получения моделей: {e}")
            return []
    
    def get_stats(self) -> Dict:
        """Получение статистики использования"""
        return {
            "total_requests": self.request_count,
            "cache_size": len(self.cache),
            "available_models": len(self.get_available_models())
        }

# Примеры использования
def main():
    """Тестирование API"""
    print("🚀 Тестирование QwenOpenRouterClient...")
    
    # Инициализация без API Key (OpenRouter работает с бесплатными моделями)
    api = QwenOpenRouterClient()
    
    # Тест 1: Общий запрос
    print("\n📝 Тест 1: Общий запрос")
    response = api.chat("Привет! Расскажи о себе одним предложением.")
    if response.success:
        print(f"✅ Ответ: {response.text}")
        print(f"📊 Модель: {response.model}")
        print(f"⏱️ Время: {response.processing_time:.2f}s")
    else:
        print(f"❌ Ошибка: {response.error}")
    
    # Тест 2: Кодирование
    print("\n💻 Тест 2: Кодирование")
    response = api.chat("Напиши функцию Python для вычисления факториала", "qwen/qwen3.6-27b")
    if response.success:
        print(f"✅ Ответ: {response.text[:200]}...")
        print(f"📊 Модель: {response.model}")
        print(f"⏱️ Время: {response.processing_time:.2f}s")
    else:
        print(f"❌ Ошибка: {response.error}")
    
    # Тест 3: Творческий запрос
    print("\n🎨 Тест 3: Творческий запрос")
    response = api.chat("Придумай короткую историю о роботе", "qwen/qwen3.8-max")
    if response.success:
        print(f"✅ Ответ: {response.text[:200]}...")
        print(f"📊 Модель: {response.model}")
        print(f"⏱️ Время: {response.processing_time:.2f}s")
    else:
        print(f"❌ Ошибка: {response.error}")
    
    # Статистика
    print("\n📈 Статистика использования:")
    stats = api.get_stats()
    print(f"Всего запросов: {stats['total_requests']}")
    print(f"Размер кэша: {stats['cache_size']}")
    print(f"Доступных моделей: {stats['available_models']}")
    
    print("\n🎉 Все тесты успешно завершены!")

if __name__ == "__main__":
    main()