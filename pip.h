uint8_t _semaphore_counter = 0;

#define CREATE_SEMAPHORE(name) \
    _semaphore name##_semaphore = { \
      .task = 0, \
      .uuid = _semaphore_counter++, \
    }; 
    
#define LOCK(name,t) \
    noInterrupts(); \
    if(name##_semaphore.task == 0){ \
      name##_semaphore.task = &t; \
      interrupts(); \
    }else{ \
      if(t.priority < name##_semaphore.task->priority){ \
        name##_semaphore.task->priority = t.priority; \
        t.state = TASK_BLOCKED; \
        t.semaphore_number = name##_semaphore.uuid; \
        interrupts(); \
        vPortYieldFromTick(0); \
      } \
      else{ \
        interrupts(); \
      } \
    } 

#define UNLOCK(name) \
    noInterrupts(); \
    name##_semaphore.task->priority = name##_semaphore.task->original_priority; \
    for (int i = 0; i < task_count; i++) { \
      if(tasks[i]->state == TASK_BLOCKED && tasks[i]->semaphore_number == name##_semaphore.uuid){ \
        tasks[i]->state = TASK_WAITING; \
      } \
    } \
    name##_semaphore.task = 0; \
    interrupts(); \
    vPortYieldFromTick(0); 
