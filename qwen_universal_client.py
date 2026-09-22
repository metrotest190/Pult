#!/usr/bin/env python3
"""
Универсальный клиент для Qwen3.8-Flash
Поддержка разных провайдеров: Puter.js, OpenRouter, DeepSeek, и др.
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

class Provider(Enum):
    """Провайдеры для работы с Qwen"""
    PUTER = "puter"
    OPENROUTER = "openrouter"
    DEEPSEEK = "deepseek"
    HUGGINGFACE = "huggingface"
    ALIBABA = "alibaba"

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

class QwenUniversalClient:
    """Универсальный клиент для работы с Qwen через разных провайдеров"""
    
    def __init__(self, provider: Provider = Provider.PUTER, api_key: Optional[str] = None):
        self.provider = provider
        self.api_key = api_key
        self.base_url = self._get_base_url()
        self.session = requests.Session()
        self.cache = {}
        self.request_count = 0
        
        # Настройка заголовков
        self._setup_headers()
        
        logger.info(f"QwenUniversalClient инициализирован для провайдера {provider.value}")
    
    def _get_base_url(self) -> str:
        """Получение базового URL для провайдера"""
        if self.provider == Provider.PUTER:
            return "https://api.puter.com"
        elif self.provider == Provider.OPENROUTER:
            return "https://openrouter.ai/api/v1"
        elif self.provider == Provider.DEEPSEEK:
            return "https://api.deepseek.com"
        elif self.provider == Provider.HUGGINGFACE:
            return "https://api-inference.huggingface.co/models"
        elif self.provider == Provider.ALIBABA:
            return "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation"
        else:
            raise ValueError(f"Неизвестный провайдер: {self.provider}")
    
    def _setup_headers(self):
        """Настройка заголовков для провайдера"""
        if self.provider == Provider.PUTER:
            self.session.headers.update({
                "Content-Type": "application/json",
                "User-Agent": "Hermes-Qwen-Universal-API/1.0"
            })
        elif self.provider == Provider.OPENROUTER:
            self.session.headers.update({
                "Content-Type": "application/json",
                "Authorization": f"Bearer {self.api_key}",
                "HTTP-Referer": "https://hermes-agent.nousresearch.com",
                "X-Title": "Hermes-Qwen-OpenRouter"
            })
        elif self.provider == Provider.DEEPSEEK:
            self.session.headers.update({
                "Content-Type": "application/json",
                "Authorization": f"Bearer {self.api_key}",
                "User-Agent": "Hermes-Qwen-DeepSeek-API/1.0"
            })
        elif self.provider == Provider.HUGGINGFACE:
            self.session.headers.update({
                "Content-Type": "application/json",
                "Authorization": f"Bearer {self.api_key}",
                "User-Agent": "Hermes-Qwen-HuggingFace-API/1.0"
            })
        elif self.provider == Provider.ALIBABA:
            self.session.headers.update({
                "Content-Type": "application/json",
                "Authorization": f"Bearer {self.api_key}",
                "User-Agent": "Hermes-Qwen-Alibaba-API/1.0"
            })
    
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
        """Отправка запроса к API провайдера"""
        start_time = time.time()
        
        try:
            if self.provider == Provider.PUTER:
                url = f"{self.base_url}/ai/chat"
                response = self.session.post(url, json=payload, timeout=30)
            elif self.provider == Provider.OPENROUTER:
                url = f"{self.base_url}/chat/completions"
                response = self.session.post(url, json=payload, timeout=30)
            elif self.provider == Provider.DEEPSEEK:
                url = f"{self.base_url}/chat/completions"
                response = self.session.post(url, json=payload, timeout=30)
            elif self.provider == Provider.HUGGINGFACE:
                url = f"{self.base_url}/qwen/qwen3.8-flash"
                response = self.session.post(url, json=payload, timeout=30)
            elif self.provider == Provider.ALIBABA:
                url = f"{self.base_url}"
                response = self.session.post(url, json=payload, timeout=30)
            else:
                raise ValueError(f"Неизвестный провайдер: {self.provider}")
            
            response.raise_for_status()
            
            processing_time = time.time() - start_time
            
            # Парсинг ответа
            result = response.json()
            
            # Извлечение текста ответа
            text = ""
            if self.provider == Provider.PUTER:
                if "text" in result:
                    text = result["text"]
                elif "response" in result:
                    text = result["response"]
            elif self.provider in [Provider.OPENROUTER, Provider.DEEPSEEK]:
                if "choices" in result and len(result["choices"]) > 0:
                    text = result["choices"][0]["message"]["content"]
            elif self.provider == Provider.HUGGINGFACE:
                if "generated_text" in result:
                    text = result["generated_text"]
                elif "text" in result:
                    text = result["text"]
            elif self.provider == Provider.ALIBABA:
                if "output" in result and "text" in result["output"]:
                    text = result["output"]["text"]
            
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
             model: str = None,
             temperature: float = 0.7,
             max_tokens: int = 1000,
             use_cache: bool = True) -> QwenResponse:
        """
        Отправка запроса к модели
        
        Args:
            message: Сообщение пользователя
            model: Модель (если None, используется модель по умолчанию для провайдера)
            temperature: Температура генерации
            max_tokens: Максимальное количество токенов
            use_cache: Использовать кэширование
            
        Returns:
            Ответ модели
        """
        # Определение модели по умолчанию
        if model is None:
            if self.provider == Provider.PUTER:
                model = "gpt-3.5-turbo"
            elif self.provider == Provider.OPENROUTER:
                model = "qwen/qwen3.8-flash"
            elif self.provider == Provider.DEEPSEEK:
                model = "deepseek-v4-flash"
            elif self.provider == Provider.HUGGINGFACE:
                model = "qwen/qwen3.8-flash"
            elif self.provider == Provider.ALIBABA:
                model = "qwen-turbo"
        
        # Проверка кэша
        if use_cache:
            cache_key = self._get_cache_key(message, model, temperature)
            cached_response = self._get_from_cache(cache_key)
            if cached_response:
                return cached_response
        
        # Формирование payload
        if self.provider in [Provider.PUTER, Provider.OPENROUTER, Provider.DEEPSEEK]:
            payload = {
                "model": model,
                "messages": [{"role": "user", "content": message}],
                "temperature": temperature,
                "max_tokens": max_tokens
            }
        elif self.provider == Provider.HUGGINGFACE:
            payload = {
                "inputs": message,
                "parameters": {
                    "temperature": temperature,
                    "max_new_tokens": max_tokens,
                    "do_sample": True
                }
            }
        elif self.provider == Provider.ALIBABA:
            payload = {
                "model": model,
                "input": {
                    "messages": [{"role": "user", "content": message}]
                },
                "parameters": {
                    "temperature": temperature,
                    "max_tokens": max_tokens
                }
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
            if self.provider == Provider.OPENROUTER:
                response = self.session.get(f"{self.base_url}/models", timeout=10)
                response.raise_for_status()
                result = response.json()
                models = []
                if "data" in result:
                    for model_info in result["data"]:
                        model_id = model_info.get("id", "")
                        if "qwen" in model_id.lower():
                            models.append(model_id)
                return models
            elif self.provider == Provider.HUGGINGFACE:
                # HuggingFace не предоставляет API для списка моделей
                return ["qwen/qwen3.8-flash", "qwen/qwen3.7-plus", "qwen/qwen3.6-27b"]
            elif self.provider == Provider.DEEPSEEK:
                # DeepSeek предоставляет модели через API
                return ["deepseek-v4-flash", "deepseek-v4-pro", "deepseek-r1"]
            else:
                return []
        except requests.exceptions.RequestException as e:
            logger.error(f"Ошибка получения моделей: {e}")
            return []
    
    def get_stats(self) -> Dict:
        """Получение статистики использования"""
        return {
            "total_requests": self.request_count,
            "cache_size": len(self.cache),
            "available_models": len(self.get_available_models()),
            "provider": self.provider.value
        }

# Примеры использования
def main():
    """Тестирование API"""
    print("🚀 Тестирование QwenUniversalClient...")
    
    # Тестирование разных провайдеров
    providers = [
        (Provider.PUTER, None),
        (Provider.OPENROUTER, "your_openrouter_api_key"),
        (Provider.DEEPSEEK, "your_deepseek_api_key"),
    ]
    
    for provider, api_key in providers:
        print(f"\n🔧 Тестирование {provider.value}...")
        
        try:
            api = QwenUniversalClient(provider, api_key)
            
            # Тестовый запрос
            response = api.chat("Привет! Расскажи о себе одним предложением.")
            if response.success:
                print(f"✅ Ответ: {response.text}")
                print(f"📊 Модель: {response.model}")
                print(f"⏱️ Время: {response.processing_time:.2f}s")
            else:
                print(f"❌ Ошибка: {response.error}")
            
            # Статистика
            stats = api.get_stats()
            print(f"📈 Статистика: {stats}")
            
        except Exception as e:
            print(f"❌ Ошибка инициализации {provider.value}: {e}")

if __name__ == "__main__":
    main()