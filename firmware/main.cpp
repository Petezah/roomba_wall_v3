/*
 * roomba_wall_v2.cpp
 *
 * Created: 5/11/2016 9:12:04 PM
 * Author : Peter Dunshee
 */ 

#include "roomba_wall_v2.h"
#include <avr/io.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

enum run_state
{
	RS_Sleeping = 0,
	RS_Running,
	RS_InitCounter,
	RS_Counter,
	RS_TimeSet
};
volatile run_state g_state = RS_Sleeping;
volatile uint32_t g_timerTicks = 0;
volatile uint32_t g_lastTimerTicks = 0;
volatile int g_counter = 0;
volatile uint8_t g_debounceDownCounter = 0;
volatile uint8_t g_debounceUpCounter = 0;
volatile uint8_t g_numPresses = 0;

#define NUM_TIMER_TICKS_PER_PRESS 3600; // run for 1/2hr per setting: 3600 = 60s * 30m * 2, since 1 tick per .5s
volatile int g_numTicksUntilDisabled = 0;

void sleep_until_interrupt()
{
	MCUCR |= _BV(SE); //sleep enable bit
	GIMSK |= _BV(PCIE); // Enable pin-change interrupts to wake us up
        sleep_mode();
	GIMSK &= ~_BV(PCIE); // Disable pin-change interrupts
        MCUCR &= ~_BV(SE); // clear sleep enable bit
}

void blink_led(uint8_t numTimes, bool fastDelay = false)
{
	PORTB &= ~_BV(0);
	for (uint8_t i=0; i<numTimes; ++i)
	{
		if (fastDelay) _delay_ms(50);
		else _delay_ms(200);
		PORTB |= _BV(0); //on
		if (fastDelay) _delay_ms(50);
		else _delay_ms(200);
		PORTB &= ~_BV(0);//off
	}
}

int main(void)
{
	// Disable ADC; we don't need it, and it uses more power
	ACSR |= _BV(ACD); // analog comparator disable

	// Set up timer1 to trigger every 1/2 second
	TCCR1 |= _BV(CTC1); // enable timer compare match CTC1
	TCCR1 |= _BV(CS13) | _BV(CS12) | _BV(CS11) | _BV(CS10); // set prescaler ck/16384 (1/(8Mhz/16384) = 2.048 ms)
	OCR1C = 244; // count to 1/2 seconds (2.048ms * 244 = .5s)
	TIMSK |= _BV(OCIE1A); // enable compare match irq

	// Set interrupt on PB2
	DDRB &= ~_BV(DDB2);   // PB2 set as input
	GIMSK |= _BV(INT0);   // enable PCIE interrupt
	PCMSK |= _BV(PCINT2); // interrupt on PB2
	MCUCR |= (_BV(ISC01) | _BV(ISC00)); // enable rising edge (ISC01 and ISC00)
	MCUCR |= _BV(SM1);    // enable power-down for sleep (SM1)
	sei(); // enable interrupts

	// Red-Status LED
	DDRB |= _BV(0);   // Set PORTB pin 4 to digital output (equivalent to pinMode(0, OUTPUT))
	PORTB &= ~_BV(0); // Pin 0 set to LOW (equivalent to digitalWrite(0, LOW))

#ifdef ROOMBA_WALL_V3
	// Power down initially
	g_state = RS_Sleeping;
        sleep_until_interrupt();
#else
	g_state = RS_Running;
#endif

	g_counter = -1;
	int i = -1;
	uint32_t lastBlinkTick = 0;
	while (1)
	{
		i = g_counter;
		switch (g_state)
		{
		case RS_Sleeping:
			g_numPresses = 0;
			sleep_until_interrupt();
			break;

		case RS_Running:
			if (g_numTicksUntilDisabled <= 0) g_state = RS_Sleeping;

			g_numPresses = 0;
			i++;
			if (i == 0) PORTB |= _BV(0);// digitalWrite(4, HIGH);
			if (i == 1) PORTB &= ~_BV(0); // digitalWrite(4, LOW);
			if (i == 5) i = -1;

			roomba_send(162); // Virtual Wall
			PORTB &= ~_BV(0); // digitalWrite(4, LOW);
#ifdef ROOMBA_WALL_V2
			delay_ten_us(50000);
#else // others (eg. Christmas barrier)
			delay_ten_us(10000);
			break;
#endif
		case RS_InitCounter:
			g_state = RS_Counter;
			lastBlinkTick = g_lastTimerTicks;
			break;

		case RS_Counter:
			i++;
			if (g_timerTicks - lastBlinkTick >= 2)
			{
				lastBlinkTick = g_timerTicks;
				blink_led(1, true);
			}
			if (g_debounceUpCounter >= 2)
			{
				g_numPresses = (g_timerTicks - g_lastTimerTicks) / 2;
				g_numPresses--;
				g_state = RS_TimeSet;
				i = 0;
			}
			break;

		case RS_TimeSet:
			i++;
			if (i >= 2) // 2 seconds
			{
				i = -1;
				g_state = RS_Running;
				blink_led(g_numPresses);
			}
			_delay_ms(2000);
			g_numTicksUntilDisabled = g_numPresses * NUM_TIMER_TICKS_PER_PRESS;
			break;
		}//switch

		g_counter = i;
	}
}

ISR(PCINT0_vect)
{
	// do nothing
}

ISR(INT0_vect)
{
	if (g_state != RS_InitCounter && g_state != RS_Counter)
	{	g_counter = 0;
		g_debounceDownCounter = 0;
		g_debounceUpCounter = 0;
		g_lastTimerTicks = g_timerTicks;
		g_state = RS_InitCounter;
		//blink_led(1);
	}
	PORTB &= ~_BV(0);
}

ISR(TIMER1_COMPA_vect)
{
	g_timerTicks++;
	g_numTicksUntilDisabled--;
	if (PINB & _BV(PB2)) g_debounceDownCounter++;
        else g_debounceUpCounter++;
}
