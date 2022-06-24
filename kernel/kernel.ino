#include <avr/interrupt.h>
#include "assembly.h"

#define d1 13 
#define d2 12
#define d3 11
#define d4 10
#define NT 20 //Max number of tasks
#define TICK_FREQUENCY      62
#define STACK_SIZE_DEFAULT  100

typedef struct {
    /* Pointer to the address of the task's 'private' stack in memory */
    volatile uint8_t*   stack_ptr;                 
    /* Size of the allocated stack in bytes */ 
    uint16_t            stack_size;
    uint8_t*            stack_array_ptr;
    /* period in ticks */
    int period;
    /* ticks until next activation */
    int delay;
    /* function pointer */
    void (*func)(void);
    /* activation counter */
    int exec;
    /* task priority */
    int priority;
    /* task scheduling state */ 
    uint8_t state;
} Sched_Task_t;

/** Task states */
enum State {
    READY,     // Ready to be executed 
    RUNNING,   // Currently executing on the processor
    WAITING,   // Task is waiting for a resource to be unlocked, like a mutex
    DONE,      // Task has completed is job. Shifts to TASK_READY in the next activation period
    DEAD       // One-shot tasks that shall not run again
};



//context switching functions
void vPortYieldFromTick( void ) __attribute__ ( ( naked ) );
uint8_t *pxPortInitialiseStack( uint8_t* pxTopOfStack, void (*pxCode)(), void *pvParameters );
// Definitions from scheduler.ino
int Sched_Init(void);
int Sched_AddT(void (*f)(void), int delay, int period, int priority, int stack_size, uint8_t* stack_start);
void Sched_Schedule(void);
void Sched_Dispatch(void);
void removeTask(int x);
void orderTasks();


#define TASK_CREATE(name, f, delay, period, priority, stack_size) \
 uint8_t name##_stack[stack_size]; \
 Sched_AddT(f, delay, period, priority, stack_size, name##_stack)

// Global variables
Sched_Task_t Tasks[NT];
uint8_t stacks[NT];
int cur_task = NT;
volatile void* volatile pxCurrentTCB = 0;

/* 4 Tasks:
 *     T1 - T = 100ms   -> Led d1 toggle
 *     T2 - T = 200ms   -> Led d2 toggle
 *     T3 - T = 600ms   -> Led d3 toggle
 *     T4 - T = 100ms   -> Led d4 copied from button A1
 */
void toggle(void) {digitalWrite(d4, !digitalRead(d4));}


void t1(void) {digitalWrite(d1, !digitalRead(d1)); Serial.write("Did task 111111");}
void t2(void) {digitalWrite(d2, !digitalRead(d2)); Serial.write("Did task 222222");}
void t3(void) {digitalWrite(d3, !digitalRead(d3)); Serial.write("Did task 333333");}
void t4(void) {digitalWrite(d4,  digitalRead(A1));}


uint8_t *pxPortInitialiseStack( uint8_t* pxTopOfStack, void (*pxCode)(), void *pvParameters ) {
    uint16_t usAddress;
    /* Simulate how the stack would look after a call to vPortYield() generated by
    the compiler. */

    /* The start of the task code will be popped off the stack last, so place
    it on first. */
    usAddress = ( uint16_t ) pxCode;
    *pxTopOfStack = ( uint8_t ) ( usAddress & ( uint16_t ) 0x00ff );
    pxTopOfStack--;

    usAddress >>= 8;
    *pxTopOfStack = ( uint8_t ) ( usAddress & ( uint16_t ) 0x00ff );
    pxTopOfStack--;

    /* Next simulate the stack as if after a call to portSAVE_CONTEXT().
    portSAVE_CONTEXT places the flags on the stack immediately after r0
    to ensure the interrupts get disabled as soon as possible, and so ensuring
    the stack use is minimal should a context switch interrupt occur. */
    *pxTopOfStack = ( uint8_t ) 0x00;    /* R0 */
    pxTopOfStack--;
    *pxTopOfStack = ( (uint8_t) 0x80 );
    pxTopOfStack--;

    /* Now the remaining registers. The compiler expects R1 to be 0. */
    *pxTopOfStack = ( uint8_t ) 0x00;    /* R1 */

    /* Leave R2 - R23 untouched */
    pxTopOfStack -= 23;

    /* Place the parameter on the stack in the expected location. */
    usAddress = ( uint16_t ) pvParameters;
    *pxTopOfStack = ( uint8_t ) ( usAddress & ( uint16_t ) 0x00ff );
    pxTopOfStack--;

    usAddress >>= 8;
    *pxTopOfStack = ( uint8_t ) ( usAddress & ( uint16_t ) 0x00ff );

    /* Leave register R26 - R31 untouched */
    pxTopOfStack -= 7;

    return pxTopOfStack;
}


// the setup function runs once when you press reset or power the board
void setup() {

  Serial.begin(9600);
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(d4, OUTPUT);
  pinMode(d3, OUTPUT);
  pinMode(d2, OUTPUT);
  pinMode(d1, OUTPUT);
  Sched_Init();
  TASK_CREATE(task1,t1, 50 /* delay */, 10000 /* period */,3,STACK_SIZE_DEFAULT);
  TASK_CREATE(task2,t2, 50 /* delay */,  10000 /* period */, 2,STACK_SIZE_DEFAULT);
  TASK_CREATE(task3,t3, 50 /* delay */,  10000 /* period */, 1,STACK_SIZE_DEFAULT);
  
  
}

/* Interrupt service routine for the RTOS tick. */
ISR(TIMER1_COMPA_vect){//timer1 interrupt

  /* Call the tick function. */
  //vPortYieldFromTick();

  Sched_Schedule();
  Sched_Dispatch();

  /* Return from the interrupt. If a context
  switch has occurred this will return to a
  different task. */
  //asm volatile ( "reti" );
}

void vPortYieldFromTick( void )
{
  /* This is a naked function so the context
  is saved. */
  portSAVE_CONTEXT();
  /* Increment the tick count and check to see
  if the new tick value has caused a delay
  period to expire. This function call can
  cause a task to become ready to run. */
  Sched_Schedule();
  /* See if a context switch is required.
  Switch to the context of a task made ready
  to run by vTaskIncrementTick() if it has a
  priority higher than the interrupted task. */
  Sched_Dispatch();
  /* Restore the context. If a context switch
  has occurred this will restore the context of
  the task being resumed. */
  portRESTORE_CONTEXT();
  /* Return from this naked function. */
  asm volatile ( "ret" );
}


// void vTaskSwitchContext() {
//     PORTD ^= _BV(STATUS_LED);    // Pisca-pisca no ma no ma ei

//     if(tasks[task]->status == TASK_RUNNING)
//         tasks[task]->status = TASK_WAITING;

//     PORTD &= ~(_BV(CS_LED)); // turn off iddle task LED

//     // find the highest priority task which is ready (i.e., task->priority is lowest)

//     uint8_t run_next_id = 0;
//     uint8_t run_next_pr = 255;
//     for(uint8_t i = 0; i < task_count; i++){
//         if(tasks[i] && tasks[i]->priority <= run_next_pr && (tasks[i]->status == TASK_READY || tasks[i]->status == TASK_WAITING) && TASK_REQUESTED_MUTEXES_ARE_UNLOCKED) {
//             run_next_id = i;
//             run_next_pr = tasks[i]->priority;
//         }
//     }

//     task = run_next_id;
//     tasks[task]->status = TASK_RUNNING;
//     pxCurrentTCB = &tasks[task]->stack_ptr;
//     if(run_next_id==0){
//         PORTD |= _BV(CS_LED); // turn on iddle task LED
//     }
    
//     return;
// }

// the loop function runs over and over again forever
void loop() {
  /* nothing to do */
}
