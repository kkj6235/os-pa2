/**********************************************************************
 * Copyright (c) 2019-2024
 *  Sang-Hoon Kim <sanghoonkim@ajou.ac.kr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTIABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "list_head.h"

/**
 * The process which is currently running
 */
#include "process.h"

extern struct process *current;

/**
 * List head to hold the processes ready to run
 */
extern struct list_head readyqueue;

/**
 * Resources in the system.
 */
#include "resource.h"

extern struct resource resources[NR_RESOURCES];

/**
 * Monotonically increasing ticks. Do not modify it
 */
extern unsigned int ticks;

/**
 * Quiet mode. True if the program was started with -q option
 */
extern bool quiet;

/***********************************************************************
 * Default FCFS resource acquision function
 *
 * DESCRIPTION
 *   This is the default resource acquision function which is called back
 *   whenever the current process is to acquire resource @resource_id.
 *   The current implementation serves the resource in the requesting order
 *   without considering the priority. See the comments in sched.h
 ***********************************************************************/
static bool fcfs_acquire(int resource_id) {
    struct resource *r = resources + resource_id;

    if (!r->owner) {
        /* This resource is not owned by any one. Take it! */
        r->owner = current;
        return true;
    }

    /* OK, this resource is taken by @r->owner. */

    /* Update the current process state */
    current->status = PROCESS_BLOCKED;

    /* And append current to waitqueue */
    list_add_tail(&current->list, &r->waitqueue);

    /**
     * And return false to indicate the resource is not available.
     * The scheduler framework will soon call schedule() function to
     * schedule out current and to pick the next process to run.
     */
    return false;
}

/***********************************************************************
 * Default FCFS resource release function
 *
 * DESCRIPTION
 *   This is the default resource release function which is called back
 *   whenever the current process is to release resource @resource_id.
 *   The current implementation serves the resource in the requesting order
 *   without considering the priority. See the comments in sched.h
 ***********************************************************************/
static void fcfs_release(int resource_id) {
    struct resource *r = resources + resource_id;

    /* Ensure that the owner process is releasing the resource */
    assert(r->owner == current);

    /* Un-own this resource */
    r->owner = NULL;

    /* Let's wake up ONE waiter (if exists) that came first */
    if (!list_empty(&r->waitqueue)) {
        struct process *waiter = list_first_entry(&r->waitqueue, struct process, list);

        /**
         * Ensure the waiter is in the wait status
         */
        assert(waiter->status == PROCESS_BLOCKED);

        /**
         * Take out the waiter from the waiting queue. Note we use
         * list_del_init() over list_del() to maintain the list head tidy
         * (otherwise, the framework will complain on the list head
         * when the process exits).
         */
        list_del_init(&waiter->list);

        /* Update the process status */
        waiter->status = PROCESS_READY;

        /**
         * Put the waiter process into ready queue. The framework will
         * do the rest.
         */
        list_add_tail(&waiter->list, &readyqueue);
    }
}

#include "sched.h"

/***********************************************************************
 * FCFS scheduler
 ***********************************************************************/
static int fcfs_initialize(void) {
    return 0;
}

static void fcfs_finalize(void) {
}

static struct process *fcfs_schedule(void) {
    struct process *next = NULL;

    /* You may inspect the situation by calling dump_status() at any time */
    // dump_status();

    /**
     * When there was no process to run in the previous tick (so does
     * in the very beginning of the simulation), there will be
     * no @current process. In this case, pick the next without examining
     * the current process. Also, the current process can be blocked
     * while acquiring a resource. In this case just pick the next as well.
     */
    if (!current || current->status == PROCESS_BLOCKED) {
        goto pick_next;
    }

    /* The current process has remaining lifetime. Schedule it again */
    if (current->age < current->lifespan) {
        return current;
    }

    pick_next:
    /* Let's pick a new process to run next */

    if (!list_empty(&readyqueue)) {
        /**
         * If the ready queue is not empty, pick the first process
         * in the ready queue
         */
        next = list_first_entry(&readyqueue, struct process, list);

        /**
         * Detach the process from the ready queue. Note that we use
         * list_del_init() over list_del() to maintain the list head tidy.
         * Otherwise, the framework will complain (assert) on process exit.
         */
        list_del_init(&next->list);
    }

    /* Return the process to run next */
    return next;
}

struct scheduler fcfs_scheduler = {
        .name = "FCFS",
        .acquire = fcfs_acquire,
        .release = fcfs_release,
        .initialize = fcfs_initialize,
        .finalize = fcfs_finalize,
        .schedule = fcfs_schedule,
};

/***********************************************************************
 * SJF scheduler
 ***********************************************************************/
static struct process *sjf_schedule(void) {
    struct process *next = NULL;


    if (!current || current->status == PROCESS_BLOCKED) {
        goto pick_next;
    }

    /* The current process has remaining lifetime. Schedule it again */
    if (current->age < current->lifespan) {
        return current;
    }

    pick_next:

    if (!list_empty(&readyqueue)) {

        struct process *p = NULL;

        list_for_each_entry(p, &readyqueue, list) {
            if (!next) {
                next = p;
            } else {
                if (p->lifespan < next->lifespan) {
                    next = p;
                }
            }
        }
        list_del_init(&next->list);
    }

    /* Return the process to run next */
    return next;
}

struct scheduler sjf_scheduler = {
        .name = "Shortest-Job First",
        .acquire = fcfs_acquire,    /* Use the default FCFS acquire() */
        .release = fcfs_release,    /* Use the default FCFS release() */
        .schedule = sjf_schedule,            /* TODO: Assign your schedule function
								   to this function pointer to activate
								   SJF in the simulation system */
};

/***********************************************************************
 * STCF scheduler
 ***********************************************************************/

static struct process *stcf_schedule(void) {
    struct process *next = NULL;

    if (!list_empty(&readyqueue)) {

        struct process *p = NULL;

        list_for_each_entry(p, &readyqueue, list) {
            if (!next) {
                next = p;
            } else {
                if ((p->lifespan - p->age) < (next->lifespan - next->age)) {
                    next = p;
                }
            }
        }
        if (!current) {
            list_del_init(&next->list);
            return next;
        }
        if (current->lifespan != current->age) {
            if ((current->lifespan - current->age) < (next->lifespan - next->age)) {
                next = current;
            } else {
                list_add_tail(&current->list, &readyqueue);
            }
        }
        list_del_init(&next->list);

    } else {
        if (current->age < current->lifespan) {
            return current;
        }
    }
    return next;
}

struct scheduler stcf_scheduler = {
        .name = "Shortest Time-to-Complete First",
        .acquire = fcfs_acquire, /* Use the default FCFS acquire() */
        .release = fcfs_release, /* Use the default FCFS release() */
        .schedule = stcf_schedule,
        /* You need to check the newly created processes to implement STCF.
         * Have a look at @forked() callback.
         */

};

/***********************************************************************
 * Round-robin scheduler
 ***********************************************************************/

static struct process *rr_schedule(void) {
    struct process *next = NULL;

    if(!list_empty(&readyqueue)){
        next = list_first_entry(&readyqueue, struct process, list);
        list_del_init(&next->list);

        if (current) {
            if (current->lifespan != current->age) {
                list_add_tail(&current->list, &readyqueue);
            }
        }
    }
    else{
        if(current->age<current->lifespan){
            return current;
        }
    }


    return next;
}


struct scheduler rr_scheduler = {
        .name = "Round-Robin",
        .acquire = fcfs_acquire, /* Use the default FCFS acquire() */
        .release = fcfs_release, /* Use the default FCFS release() */
        .schedule = rr_schedule,

        /* Obviously, ... */
};

/***********************************************************************
 * Priority scheduler
 ***********************************************************************/
static bool prio_acquire(int resource_id) {
    struct resource *r = resources + resource_id;

    if (!r->owner) {
        /* This resource is not owned by any one. Take it! */
        r->owner = current;
        return true;
    }
    list_del_init(&current->list);
    list_add_tail(&current->list, &r->waitqueue);
    current->status = PROCESS_BLOCKED;

    return false;
}
static void prio_release(int resource_id) {
    struct resource *r = resources + resource_id;
    struct process *waiter = NULL;
    assert(r->owner == current);

    r->owner = NULL;
    if (!list_empty(&r->waitqueue)) {

        struct process *p = NULL;
        list_for_each_entry(p,&r->waitqueue , list) {
            if (!waiter) {
                waiter = p;
            }
            else {
                if (p->prio > waiter->prio) {
                    waiter = p;
                }
            }

        }

        assert(waiter->status == PROCESS_BLOCKED);

        list_del_init(&waiter->list);
        waiter->status = PROCESS_READY;

        list_add_tail(&waiter->list, &readyqueue);
    }
}
static struct process *prio_schedule(void) {

    struct process *next = NULL;
    struct process *p = NULL;

    if(!list_empty(&readyqueue)){

        list_for_each_entry(p, &readyqueue, list) {
            if (!next) {
                next = p;
            }
            else {
                if (p->prio > next->prio && p->status==PROCESS_READY) {
                    next = p;
                }
            }
        }

        if (!current || current->age==current->lifespan) {
            list_del_init(&next->list);
            return next;
        }
        else {
            if(current->status!=PROCESS_BLOCKED){

                if(current->prio>next->prio){
                    return current;
                }
                if(current->age!=current->lifespan){
                    list_add_tail(&current->list, &readyqueue);
                }
            }

            list_del_init(&next->list);
            return next;
        }
    }
    else{
        if(current->age<current->lifespan){
            return current;
        }
    }
    return next;
}

struct scheduler prio_scheduler = {
        .name = "Priority",
        .acquire = prio_acquire,
        .release = prio_release,
        .schedule = prio_schedule,

        /**
         * Implement your own acqure/release function to make the priority
         * scheduler correct.
         */
        /* Implement your own prio_schedule() and attach it here */
};

/***********************************************************************
 * Priority scheduler with aging
 ***********************************************************************/
static bool pa_acquire(int resource_id) {
    struct resource *r = resources + resource_id;

    if (!r->owner) {
        /* This resource is not owned by any one. Take it! */
        r->owner = current;
        return true;
    }
    if(current->prio < r->owner->prio){
        current->status = PROCESS_BLOCKED;
        list_add_tail(&current->list, &r->waitqueue);
    }
    else{
        r->owner->status = PROCESS_BLOCKED;
        list_add_tail(&r->owner->list, &r->waitqueue);
        r->owner = current;
        return true;
    }
    return false;
}
static void pa_release(int resource_id) {
    struct resource *r = resources + resource_id;
    struct process *waiter = NULL;

    assert(r->owner == current);

    r->owner = NULL;

    if (!list_empty(&r->waitqueue)) {

        struct process *p = NULL;

        list_for_each_entry(p, &readyqueue, list) {
            if (!waiter) {
                waiter = p;
            } else {
                if (p->prio > waiter->prio) {
                    waiter = p;
                }
            }
        }

        assert(waiter->status == PROCESS_BLOCKED);

        list_del_init(&waiter->list);

        waiter->status = PROCESS_READY;

        list_add_tail(&waiter->list, &readyqueue);
    }
}
static struct process *pa_schedule(void) {
    struct process *next = NULL;
    struct process *p = NULL;

    if (!current || current->status == PROCESS_BLOCKED) {
        goto pick_next;
    }
    if(!list_empty(&readyqueue)){
        current->prio = current->prio_orig;

        list_for_each_entry(p, &readyqueue, list) {
            if(p->prio<MAX_PRIO){
                p->prio++;
            }

            if (!next) {
                next = p;
            }
            else{
                if(p->prio>next->prio){
                    next = p;
                }
            }
        }

        if(current->age < current->lifespan){
            if(next->prio<current->prio){
                return current;
            }
            list_add_tail(&current->list,&readyqueue);
        }

        list_del_init(&next->list);

        return next;
    }
    else{
        if(current->age < current->lifespan){
            return current;
        }
        return NULL;
    }


pick_next:

    if(!list_empty(&readyqueue)){
        list_for_each_entry(p, &readyqueue, list) {
            if (!next) {
                next = p;
            } else {
                if (p->prio > next->prio) {
                    next = p;
                }
            }
        }
        list_del_init(&next->list);
    }


    return next;
}
struct scheduler pa_scheduler = {
        .name = "Priority + aging",
        .acquire = pa_acquire,
        .release = pa_release,
        .schedule = pa_schedule,
};

/***********************************************************************
 * Priority scheduler with priority ceiling protocol
 ***********************************************************************/
static bool pcp_acquire(int resource_id) {
    struct resource *r = resources + resource_id;

    if (!r->owner) {
        /* This resource is not owned by any one. Take it! */
        r->owner = current;
        r->owner->prio = MAX_PRIO;
        return true;
    }
    list_del_init(&current->list);
    current->status = PROCESS_BLOCKED;

    list_add_tail(&current->list, &r->waitqueue);

    return false;
}
static void pcp_release(int resource_id) {

    struct resource *r = resources + resource_id;
    struct process *waiter = NULL;

    r->owner->prio=r->owner->prio_orig;

    assert(r->owner == current);

    r->owner = NULL;

    /* Let's wake up ONE waiter (if exists) that came first */
    if (!list_empty(&r->waitqueue)) {
        struct process *p = NULL;

        list_for_each_entry(p, &r->waitqueue, list) {
            if (!waiter) {
                waiter = p;
            } else {
                if (p->prio > waiter->prio) {
                    waiter = p;
                }
            }
        }
        assert(waiter->status == PROCESS_BLOCKED);

        list_del_init(&waiter->list);

        waiter->status = PROCESS_READY;
        list_add_tail(&waiter->list, &readyqueue);
    }

}
static struct process *pcp_schedule(void) {
    struct process *next = NULL;
    struct process *p = NULL;


    if(!list_empty(&readyqueue)){

        list_for_each_entry(p, &readyqueue, list) {
            if (!next) {
                next = p;
            } else {
                if (p->prio > next->prio) {
                    next = p;
                }
            }
        }
        if(!current){
            list_del_init(&next->list);
            return next;
        }
        if(current->age<current->lifespan){
            if(current->prio<=next->prio){
                if(current->status!=PROCESS_BLOCKED){
                    list_add_tail(&current->list, &readyqueue);
                }
                list_del_init(&next->list);
                return next;

            }
            else{
                return current;

            }

        }

        list_del_init(&next->list);
    }
    else{

        if(current->age<current->lifespan){
            return current;
        }
    }
    return next;
}
struct scheduler pcp_scheduler = {
        .name = "Priority + PCP Protocol",
        .acquire = pcp_acquire,
        .release = pcp_release,
        .schedule = pcp_schedule,
};

/***********************************************************************
 * Priority scheduler with priority inheritance protocol
 ***********************************************************************/
static bool pip_acquire(int resource_id) {
    struct resource *r = resources + resource_id;
    if (!r->owner) {
        r->owner = current;
        return true;
    }
    if(current->prio>r->owner->prio){
        r->owner->prio = current->prio;
    }
    list_del_init(&current->list);
    current->status = PROCESS_BLOCKED;
    list_add_tail(&current->list, &r->waitqueue);

    return false;
}
static void pip_release(int resource_id) {

    struct resource *r = resources + resource_id;
    struct process *waiter = NULL;

    assert(r->owner == current);

    r->owner->prio = r->owner->prio_orig;

    r->owner = NULL;

    /* Let's wake up ONE waiter (if exists) that came first */
    if (!list_empty(&r->waitqueue)) {
        struct process *p = NULL;

        list_for_each_entry(p, &r->waitqueue, list) {
            if (!waiter) {
                waiter = p;
            } else {
                if (p->prio > waiter->prio) {
                    waiter = p;
                }
            }
        }
        assert(waiter->status == PROCESS_BLOCKED);

        list_del_init(&waiter->list);

        waiter->status = PROCESS_READY;

        list_add_tail(&waiter->list, &readyqueue);
    }

}
static struct process *pip_schedule(void) {
    struct process *next = NULL;

    if(!list_empty(&readyqueue)){
        struct process *p = NULL;

        list_for_each_entry(p, &readyqueue, list) {
            if (!next) {
                next = p;
            } else {
                if (p->prio > next->prio) {
                    next = p;
                }
            }
        }
        if(!current){
            list_del_init(&next->list);
            return next;
        }
        if(current->age<current->lifespan){

            if(current->prio<=next->prio){

                if(current->status!=PROCESS_BLOCKED){
                    list_add_tail(&current->list, &readyqueue);
                }
                list_del_init(&next->list);
                return next;

            }
            else{
                if(current->status==PROCESS_BLOCKED){
                    list_del_init(&next->list);
                    return next;
                }
                return current;
            }

        }

        list_del_init(&next->list);
    }
    else{
        if(current->age<current->lifespan){
            return current;
        }
    }


    return next;
}
struct scheduler pip_scheduler = {
        .name = "Priority + PIP Protocol",
        .acquire = pip_acquire,
        .release = pip_release,
        .schedule = pip_schedule,
};
