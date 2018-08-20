/*
 * roomba_wall_v2.cpp
 *
 * Created: 5/11/2016 9:12:04 PM
 * Author : Peter Dunshee
 */

#include "roomba_wall_v3.h"
#include <avr/io.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

enum run_state
{
	RS_Sleeping = 0,	// device sleeping
	RS_Running,			// running, normal
	RS_RunningShutdown, // running, in possible shut-down process
	RS_InitCounter,		// start-up, init the counter process
	RS_Counter,			// running the counter process
	RS_TimeSet			// setting the timer, proceeds to running state
};
volatile run_state g_state = RS_Sleeping;   // current state-machine state
volatile uint32_t g_timerTicks = 0;			// number of TIMER1 ticks since power-on
volatile uint32_t g_lastTimerTicks = 0;		// used to time operations in various places
volatile int g_counter = 0;					// saves the state-counter value between iterations
volatile uint8_t g_debounceDownCounter = 0; // how long the button has been depressed
volatile uint8_t g_debounceUpCounter = 0;   // how long the button has been released
volatile uint8_t g_setTimeCount = 0;		// records the number of time units the user has set the device to run for

#ifdef HALF_HOUR_PER_BLINK
#define NUM_TIMER_TICKS_PER_PRESS 3600; // run for 1/2hr per setting: 3600 = 60s * 30m * 2, since 1 tick per .5s
#else									// QUARTER_HOUR_PER_BLINK
#define NUM_TIMER_TICKS_PER_PRESS 1800; // run for 1/4hr per setting: 1800 = 60s * 15m * 2, since 1 tick per .5s
#endif
#define DEFAULT_NUM_TICKS_UNTIL_DISABLED (3 /*hours*/ * 60 /*minutes*/ * 60 /*seconds*/ * 2 /*seconds per tick*/)
volatile int g_numTicksUntilDisabled = 0;

#ifdef ROOMBA_WALL_V2
#define IR_TX_DELAY 50000
#elif ROOMBA_WALL_V3
#define IR_TX_DELAY 25000
#else // others (eg. Christmas barrier)
#define IR_TX_DELAY 10000
#endif

void sleep_until_interrupt()
{
	MCUCR |= _BV(SE);   //sleep enable bit
	GIMSK |= _BV(PCIE); // Enable pin-change interrupts to wake us up
	sleep_mode();
	GIMSK &= ~_BV(PCIE); // Disable pin-change interrupts
	MCUCR &= ~_BV(SE);   // clear sleep enable bit
}

void blink_led(uint8_t numTimes, bool fastDelay = false)
{
	PORTB &= ~_BV(0);
	for (uint8_t i = 0; i < numTimes; ++i)
	{
		if (fastDelay)
			_delay_ms(50);
		else
			_delay_ms(200);
		PORTB |= _BV(0); //on
		if (fastDelay)
			_delay_ms(50);
		else
			_delay_ms(200);
		PORTB &= ~_BV(0); //off
	}
}

int main(void)
{
	// Disable ADC; we don't need it, and it uses more power
	ACSR |= _BV(ACD); // analog comparator disable

	// Set up timer1 to trigger every 1/2 second
	TCCR1 |= _BV(CTC1);										// enable timer compare match CTC1
	TCCR1 |= _BV(CS13) | _BV(CS12) | _BV(CS11) | _BV(CS10); // set prescaler ck/16384 (1/(8Mhz/16384) = 2.048 ms)
	OCR1C = 244;											// count to 1/2 seconds (2.048ms * 244 = .5s)
	TIMSK |= _BV(OCIE1A);									// enable compare match irq

	// Set interrupt on PB2
	DDRB &= ~_BV(DDB2);					// PB2 set as input
	GIMSK |= _BV(INT0);					// enable PCIE interrupt
	PCMSK |= _BV(PCINT2);				// interrupt on PB2
	MCUCR |= (_BV(ISC01) | _BV(ISC00)); // enable rising edge (ISC01 and ISC00)
	MCUCR |= _BV(SM1);					// enable power-down for sleep (SM1)
	sei();								// enable interrupts

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
			g_setTimeCount = 0;
			sleep_until_interrupt();
			break;

		case RS_RunningShutdown:
			// If the button has been pressed for more than 2 seconds,
			// blink, then shut down the device
			if (g_debounceDownCounter >= 4)
			{
				blink_led(3, true);
				g_state = RS_Sleeping;
				i = 0;
			}
		case RS_Running:
			if (g_numTicksUntilDisabled <= 0)
				g_state = RS_Sleeping;

			g_setTimeCount = 0;
			i++;
			if (i == 0)
				PORTB |= _BV(0); // digitalWrite(4, HIGH);
			if (i == 1)
				PORTB &= ~_BV(0); // digitalWrite(4, LOW);
			if (i == 5)
				i = -1;

			roomba_send(162); // Virtual Wall
			PORTB &= ~_BV(0); // digitalWrite(4, LOW);

			delay_ten_us(IR_TX_DELAY);
			break;
		case RS_InitCounter:
			// Initialize the time setter and proceed to the counter phase
			g_state = RS_Counter;
			lastBlinkTick = g_lastTimerTicks;
			break;

		case RS_Counter:
			i++;
			// If it has been more than 1 second (1 tick == .5s)
			// indicate to the user that another "count" has happened
			if (g_timerTicks - lastBlinkTick >= 2)
			{
				lastBlinkTick = g_timerTicks;
				blink_led(1, true);
			}
			// If the button has been released for more than 1 second,
			// proceed to time-setting phase based on our current count
			if (g_debounceUpCounter >= 2)
			{
				g_setTimeCount = (g_timerTicks - g_lastTimerTicks) / 2;
				g_setTimeCount--; // adjust for fudge-error
				g_state = RS_TimeSet;
				i = 0;
			}
			break;

		case RS_TimeSet:
			if (g_setTimeCount < 1)
			{
				// Button was pressed and released; merely start the default timer
				g_numTicksUntilDisabled = DEFAULT_NUM_TICKS_UNTIL_DISABLED;
				g_state = RS_Running;

				// Turn on the LED for 1 second to indicate success of default state
				PORTB |= _BV(0); //on
				_delay_ms(1000);
				PORTB &= ~_BV(0); //off
				break;
			}

			i++;
			if (i >= 2) // 2 seconds
			{
				i = -1;
				g_state = RS_Running;
				blink_led(g_setTimeCount); // Indicate to the user how much time we have been actually set for
			}
			_delay_ms(2000);
			g_numTicksUntilDisabled = g_setTimeCount * NUM_TIMER_TICKS_PER_PRESS;
			break;
		} //switch

		g_counter = i;
	}
}

ISR(PCINT0_vect)
{
	// do nothing
}

ISR(INT0_vect)
{
	// If we are running, the button will turn off the device
	if (g_state == RS_Running)
	{
		g_counter = 0;
		g_debounceDownCounter = 0;
		g_debounceUpCounter = 0;
		g_lastTimerTicks = g_timerTicks;
		g_state = RS_RunningShutdown;
	}
	// If we are not already initializing or starting the
	// timer-set counter, then initialize the timer-setter
	else if (g_state != RS_InitCounter && g_state != RS_Counter && g_state != RS_RunningShutdown)
	{
		g_counter = 0;
		g_debounceDownCounter = 0;
		g_debounceUpCounter = 0;
		g_lastTimerTicks = g_timerTicks;
		g_state = RS_InitCounter;
	}
	PORTB &= ~_BV(0);
}

ISR(TIMER1_COMPA_vect)
{
	g_timerTicks++;
	g_numTicksUntilDisabled--;
	if (PINB & _BV(PB2))
		g_debounceDownCounter++;
	else
		g_debounceUpCounter++;
}
