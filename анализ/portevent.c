/*
 * FreeModbus Library: bare-metal event port for STM32.
 *
 * Events may be posted from USART/TIM interrupt context and consumed by
 * eMBPoll() in the foreground. The queue is therefore protected by the port
 * critical-section primitives.
 */

#include "port.h"
#include "mb.h"
#include "mbport.h"

#define MB_EVENT_QUEUE_LEN 8U

/* All access is made while interrupts are masked. volatile documents that the
   variables are shared with ISR context and prevents unsafe cache assumptions. */
static volatile eMBEventType eventQueue[MB_EVENT_QUEUE_LEN];
static volatile uint8_t eventHead;
static volatile uint8_t eventTail;
static volatile uint8_t eventCount;

static uint8_t prvNextIndex(uint8_t index)
{
    index++;
    return (index >= MB_EVENT_QUEUE_LEN) ? 0U : index;
}

BOOL xMBPortEventInit(void)
{
    ENTER_CRITICAL_SECTION();
    eventHead = 0U;
    eventTail = 0U;
    eventCount = 0U;
    EXIT_CRITICAL_SECTION();

    return TRUE;
}

BOOL xMBPortEventPost(eMBEventType eEvent)
{
    BOOL result = FALSE;

    ENTER_CRITICAL_SECTION();
    if (eventCount < MB_EVENT_QUEUE_LEN) {
        eventQueue[eventTail] = eEvent;
        eventTail = prvNextIndex(eventTail);
        eventCount++;
        result = TRUE;
    }
    EXIT_CRITICAL_SECTION();

    /* A FALSE result tells FreeModbus that an event could not be queued. The
       caller must not silently overwrite an earlier event. */
    return result;
}

BOOL xMBPortEventGet(eMBEventType *eEvent)
{
    BOOL result = FALSE;

    if (eEvent == NULL) {
        return FALSE;
    }

    ENTER_CRITICAL_SECTION();
    if (eventCount != 0U) {
        *eEvent = eventQueue[eventHead];
        eventHead = prvNextIndex(eventHead);
        eventCount--;
        result = TRUE;
    }
    EXIT_CRITICAL_SECTION();

    return result;
}
