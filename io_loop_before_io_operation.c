/* Copyright (C) 1992-2010 I/O Performance, Inc. and the
 * United States Departments of Energy (DoE) and Defense (DoD)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named 'Copying'; if not, write to
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139.
 */
/* Principal Author:
 *      Tom Ruwart (tmruwart@ioperformance.com)
 * Contributing Authors:
 *       Steve Hodson, DoE/ORNL
 *       Steve Poole, DoE/ORNL
 *       Bradly Settlemyer, DoE/ORNL
 *       Russell Cattelan, Digital Elves
 *       Alex Elder
 * Funding and resources provided by:
 * Oak Ridge National Labs, Department of Energy and Department of Defense
 *  Extreme Scale Systems Center ( ESSC ) http://www.csm.ornl.gov/essc/
 *  and the wonderful people at I/O Performance, Inc.
 */
#include "xdd.h"

//******************************************************************************
// Before I/O Operation
//******************************************************************************
/*----------------------------------------------------------------------------*/
/* xdd_syncio_before_io_operation() - This subroutine will enter the syncio 
 * barrier if it has reached the syncio-number-of-ops. Once all the other 
 * threads enter this barrier then they will all get released and I/O starts 
 * up again.
 */
void
xdd_syncio_before_io_operation(ptds_t *p) {


	if ((xgp->syncio > 0) && 
	    (xgp->number_of_targets > 1) && 
	    (p->my_current_op % xgp->syncio == 0)) {
		xdd_barrier(&xgp->syncio_barrier[p->syncio_barrier_index]);
		p->syncio_barrier_index ^= 1; /* toggle barrier index */
	}


} // End of xdd_syncio_before_io_operation()

/*----------------------------------------------------------------------------*/
/* xdd_start_trigger_before_io_operation() - This subroutine will wait for a 
 * trigger and signal another target to start if necessary.
 * Return Values: 0 is good
 *                1 is bad
 */
int32_t
xdd_start_trigger_before_io_operation(ptds_t *p) {
	ptds_t		*p2;	// Ptr to the ptds that we need to trigger
	pclk_t		tt;	// Trigger Time
	trigger_t 	*tgp;	// Pointer to trigger information
	trigger_t 	*tgp2;	// Pointer to trigger information


	

	if (p->tgp == NULL)
		return(SUCCESS);
	tgp = p->tgp;

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"start_trigger_before_io_operation: enter, p=0x%x\n",p);

	/* Check to see if we need to wait for another target to trigger us to start.
 	* If so, then enter the trigger_start barrier and have a beer until we get the signal to
 	* jump into action!
 	*/
	if ((p->target_options & TO_WAITFORSTART) && (p->run_status == 0)) { 
		/* Enter the barrier and wait to be told to go */
		xdd_barrier(&tgp->Start_Trigger_Barrier[tgp->Start_Trigger_Barrier_index]);
		tgp->Start_Trigger_Barrier_index ^= 1;
		p->run_status = 1; /* indicate that we have been released */
		return(1);
	}

	/* Check to see if we need to signal some other target to start, stop, or pause.
	 * If so, tickle the appropriate semaphore for that target and get on with our business.
	 */
	if (tgp->trigger_types) {
		p2 = xgp->ptdsp[tgp->start_trigger_target];
		tgp2 = p2->tgp;
		if (p2->run_status == 0) {
			if (tgp->trigger_types & TRIGGER_STARTTIME) {
			/* If we are past the start time then signal the specified target to start */
				pclk_now(&tt);
				if (tt > (tgp->start_trigger_time + p->my_pass_start_time)) {
					xdd_barrier(&tgp2->Start_Trigger_Barrier[tgp2->Start_Trigger_Barrier_index]);
				}
			}
			if (tgp->trigger_types & TRIGGER_STARTOP) {
				/* If we are past the specified operation, then signal the specified target to start */
				if (p->my_current_op > tgp->start_trigger_op) {
					xdd_barrier(&tgp2->Start_Trigger_Barrier[tgp2->Start_Trigger_Barrier_index]);
				}
			}
			if (tgp->trigger_types & TRIGGER_STARTPERCENT) {
				/* If we have completed percentage of operations then signal the specified target to start */
				if (p->my_current_op > (tgp->start_trigger_percent * p->target_ops)) {
					xdd_barrier(&tgp2->Start_Trigger_Barrier[tgp2->Start_Trigger_Barrier_index]);
				}
			}
			if (tgp->trigger_types & TRIGGER_STARTBYTES) {
				/* If we have completed transferring the specified number of bytes, then signal the 
				* specified target to start 
				*/
				if (p->my_current_bytes_xfered > tgp->start_trigger_bytes) {
					xdd_barrier(&tgp2->Start_Trigger_Barrier[tgp2->Start_Trigger_Barrier_index]);
				}
			}
		}
	} /* End of the trigger processing */

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"start_trigger_before_io_operation: exit, p=0x%x\n",p);

} // End of xdd_start_trigger_before_io_operation()

/*----------------------------------------------------------------------------*/
/* xdd_lockstep_before_io_operation(ptds_t *p) {
 */
int32_t	// Lock Step Processing
xdd_lockstep_before_io_operation(ptds_t *p) {
	int32_t	ping_slave;			// indicates whether or not to ping the slave 
	int32_t	slave_wait;			// indicates the slave should wait before continuing on 
	pclk_t   time_now;			// used by the lock step functions 
	ptds_t	*p2;				// Secondary PTDS pointer
	lockstep_t	*lsp;
	lockstep_t	*lsp2;


	if (p->lsp == NULL)
		return(0); // Returning 0 in this case is goodness
	lsp = p->lsp;

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"lockstep_before_io_operation: enter, p=0x%x\n",p);

	/* Check to see if we are in lockstep with another target.
	 * If so, then if we are a master, then do the master thing.
	 * Then check to see if we are also a slave and do the slave thing.
	 */
	if (lsp->ls_slave >= 0) { /* Looks like we are the MASTER in lockstep with a slave target */
		p2 = xgp->ptdsp[lsp->ls_slave]; /* This is a pointer to the slave ptds */
		lsp2 = p2->lsp;
		ping_slave = FALSE;
		/* Check to see if it is time to ping the slave to do something */
		if (lsp->ls_interval_type & LS_INTERVAL_TIME) {
			/* If we are past the start time then signal the specified target to start.
			 */
			pclk_now(&time_now);
			if (time_now > (lsp->ls_interval_value + lsp->ls_interval_base_value)) {
				ping_slave = TRUE;
				lsp->ls_interval_base_value = time_now; /* reset the new base time */
			}
		}
		if (lsp->ls_interval_type & LS_INTERVAL_OP) {
			/* If we are past the specified operation, then signal the specified target to start.
			 */
			if (p->my_current_op >= (lsp->ls_interval_value + lsp->ls_interval_base_value)) {
				ping_slave = TRUE;
				lsp->ls_interval_base_value = p->my_current_op;
			}
		}
		if (lsp->ls_interval_type & LS_INTERVAL_PERCENT) {
			/* If we have completed percentage of operations then signal the specified target to start.
			 */
			if (p->my_current_op >= ((lsp->ls_interval_value*lsp->ls_interval_base_value) * p->target_ops)) {
				ping_slave = TRUE;
				lsp->ls_interval_base_value++;
			}
		}
		if (lsp->ls_interval_type & LS_INTERVAL_BYTES) {
			/* If we have completed transferring the specified number of bytes, then signal the 
			 * specified target to start.
			 */
			if (p->my_current_bytes_xfered >= (lsp->ls_interval_value + (int64_t)(lsp->ls_interval_base_value))) {
				ping_slave = TRUE;
				lsp->ls_interval_base_value = p->my_current_bytes_xfered;
			}
		}
		if (ping_slave) { /* Looks like it is time to ping the slave to do something */
			/* note that the SLAVE owns the mutex and the counter used to tell it to do something */
			/* Get the mutex lock for the ls_task_counter and increment the counter */
			pthread_mutex_lock(&lsp2->ls_mutex);
			/* At this point the slave could be in one of three places. Thus this next section of code
			 * must be carefully structured to prevent a race condition between the master and the slave.
			 * If the slave is simply waiting for the master to release it, then the ls_task_counter lock
			 * can be released and the master can enter the lock_step_barrier to get the slave going again.
			 * If the slave is running, then it will have to wait for the ls_task_counter lock and
			 * when it gets the lock, it will discover that the ls_task_counter is non-zero and will simply
			 * decrement the counter by 1 and continue on executing a lock. 
			 */
			lsp2->ls_task_counter++;
			/* Now that we have updated the slave's task counter, we need to check to see if the slave is 
			 * waiting for us to give it a kick. If so, enter the barrier which will release the slave and
			 * us as well. 
			 * ****************Currently this is set up simply to do overlapped lockstep.*******************
			 */
			if (lsp2->ls_ms_state & LS_SLAVE_WAITING) { /* looks like the slave is waiting for something to do */
				lsp2->ls_ms_state &= ~LS_SLAVE_WAITING;
				pthread_mutex_unlock(&lsp2->ls_mutex);
				xdd_barrier(&lsp2->Lock_Step_Barrier[lsp2->Lock_Step_Barrier_Master_Index]);
				lsp2->Lock_Step_Barrier_Master_Index ^= 1;
				/* At this point the slave outght to be running. */
			} else {
				pthread_mutex_unlock(&lsp2->ls_mutex);
			}
		} /* Done pinging the slave */
	} /* end of SLAVE processing as a MASTER */
	if (lsp->ls_master >= 0) { /* Looks like we are a slave to some other MASTER target */
		/* As a slave, we need to check to see if we should stop running at this point. If not, keep on truckin'.
		 */   
		/* Look at the type of task that we need to perform and the quantity. If we have exhausted
		 * the quantity of operations for this interval then enter the lockstep barrier.
		 */
		pthread_mutex_lock(&lsp->ls_mutex);
		if (lsp->ls_task_counter > 0) {
			/* Ahhhh... we must be doing something... */
			slave_wait = FALSE;
			if (lsp->ls_task_type & LS_TASK_TIME) {
				/* If we are past the start time then signal the specified target to start.
				*/
				pclk_now(&time_now);
				if (time_now > (lsp->ls_task_value + lsp->ls_task_base_value)) {
					slave_wait = TRUE;
					lsp->ls_task_base_value = time_now;
					lsp->ls_task_counter--;
				}
			}
			if (lsp->ls_task_type & LS_TASK_OP) {
				/* If we are past the specified operation, then indicate slave to wait.
				*/
				if (p->my_current_op >= (lsp->ls_task_value + lsp->ls_task_base_value)) {
					slave_wait = TRUE;
					lsp->ls_task_base_value = p->my_current_op;
					lsp->ls_task_counter--;
				}
			}
			if (lsp->ls_task_type & LS_TASK_PERCENT) {
				/* If we have completed percentage of operations then indicate slave to wait.
				*/
				if (p->my_current_op >= ((lsp->ls_task_value * lsp->ls_task_base_value) * p->target_ops)) {
					slave_wait = TRUE;
					lsp->ls_task_base_value++;
					lsp->ls_task_counter--;
				}
			}
			if (lsp->ls_task_type & LS_TASK_BYTES) {
				/* If we have completed transferring the specified number of bytes, then indicate slave to wait.
				*/
				if (p->my_current_bytes_xfered >= (lsp->ls_task_value + (int64_t)(lsp->ls_task_base_value))) {
					slave_wait = TRUE;
					lsp->ls_task_base_value = p->my_current_bytes_xfered;
					lsp->ls_task_counter--;
				}
			}
		} else {
			slave_wait = TRUE;
		}
		if (slave_wait) { /* At this point it looks like the slave needs to wait for the master to tell us to do something */
			/* Get the lock on the task counter and check to see if we need to actually wait or just slide on
			 * through to the next task.
			 */
			/* slave_wait will be set for one of two reasons: either the task counter is zero or the slave
			 * has finished the last task and needs to wait for the next ping from the master.
			 */
			/* If the master has finished then don't bother waiting for it to enter this barrier because it never will */
			if ((lsp->ls_ms_state & LS_MASTER_FINISHED) && (lsp->ls_ms_state & LS_SLAVE_COMPLETE)) {
				/* if we need to simply finish all this, just release the lock and move on */
				lsp->ls_ms_state &= ~LS_SLAVE_WAITING; /* Slave is no longer waiting for anything */
				if (lsp->ls_ms_state & LS_MASTER_WAITING) {
					lsp->ls_ms_state &= ~LS_MASTER_WAITING;
					pthread_mutex_unlock(&lsp->ls_mutex);
					xdd_barrier(&lsp->Lock_Step_Barrier[lsp->Lock_Step_Barrier_Slave_Index]);
					lsp->Lock_Step_Barrier_Slave_Index ^= 1;
					lsp->ls_slave_loop_counter++;
				} else {
					pthread_mutex_unlock(&lsp->ls_mutex);
				}
			} else if ((lsp->ls_ms_state & LS_MASTER_FINISHED) && (lsp->ls_ms_state & LS_SLAVE_STOP)) {
				/* This is the case where the master is finished and we need to stop NOW.
				 * Therefore, release the lock, set the my_pass_ring to true and break this loop.
				 */
				lsp->ls_ms_state &= ~LS_SLAVE_WAITING; /* Slave is no longer waiting for anything */
				if (lsp->ls_ms_state & LS_MASTER_WAITING) {
					lsp->ls_ms_state &= ~LS_MASTER_WAITING;
					pthread_mutex_unlock(&lsp->ls_mutex);
					xdd_barrier(&lsp->Lock_Step_Barrier[lsp->Lock_Step_Barrier_Slave_Index]);
					lsp->Lock_Step_Barrier_Slave_Index ^= 1;
					lsp->ls_slave_loop_counter++;
				} else {
					pthread_mutex_unlock(&lsp->ls_mutex);
				}
				p->my_pass_ring = TRUE;
				return(1);
			} else { /* At this point the master is not finished and we need to wait for something to do */
				if (lsp->ls_ms_state & LS_MASTER_WAITING)
					lsp->ls_ms_state &= ~LS_MASTER_WAITING;
				lsp->ls_ms_state |= LS_SLAVE_WAITING;
				pthread_mutex_unlock(&lsp->ls_mutex);
				xdd_barrier(&lsp->Lock_Step_Barrier[lsp->Lock_Step_Barrier_Slave_Index]);
				lsp->Lock_Step_Barrier_Slave_Index ^= 1;
				lsp->ls_slave_loop_counter++;
			}
		} else {
			pthread_mutex_unlock(&lsp->ls_mutex);
		}
		/* At this point the slave does not have to wait for anything so keep on truckin... */

	} // End of being the master in a lockstep op
if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"lockstep_before_io_operation: exit, p=0x%x\n",p);
	return(0);

} // xdd_lockstep_before_io_operation()

/*----------------------------------------------------------------------------*/
/* xdd_dio_before_io_operation(ptds_t *p)
 * This routine will check several conditions to make sure that DIO will work
 * for this particular I/O operation. If any of the DIO conditions are not met
 * then DIO is turned off for this operation and all subsequent operations by
 * this qthread. 
 */
void	// DirectIO checking
xdd_dio_before_io_operation(ptds_t *p) {
	int		pagesize;
	int		status;

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"dio_before_io_operation: enter, p=0x%x\n",p);

	// Check to see if DIO is enable for this I/O - return if no DIO required
	if (!(p->target_options & TO_DIO)) 
		return;

	// If this is an SG device with DIO turned on for whatever reason then just exit
	if (p->target_options & TO_SGIO)
		return;

	// Check to see if this I/O location is aligned on the proper boundary
	pagesize = getpagesize();
	status = 0;
	if ((p->my_current_op == (p->target_ops - 1)) && // This is the last IO Operation
	    (p->last_iosize)) { // there is a short I/O at the end
		if (p->last_iosize % pagesize) // If the last io size is strange 
			status = 1; // Indicate a problem
		if (p->my_current_byte_location % pagesize) // If the I/O starts on a strange boundary
			status = 1; // Indicate a problem
	}
if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"dio_before_io_operation: status=%d, pagesize=%d, p->lastiosize=%d p=0x%x\n",status,pagesize,p->last_iosize,p);
		
	// If all the above checks passed then return
	if (status == 0)
		return;

	// At this point one or more of the above checks failed.
	// It is necessary to close and reopen this target file with DirectIO disabaled
	p->target_options &= ~TO_DIO;
	close(p->fd);
	p->fd = 0;
	p->fd = xdd_open_target(p);
	if ((unsigned int)p->fd == -1) { /* error openning target */
		fprintf(xgp->errout,"%s: xdd_dio_before_io_operation: ERROR: Reopen of target %d <%s> failed\n",xgp->progname,p->my_target_number,p->target);
		fflush(xgp->errout);
		xgp->abort_io = 1;
	}
	// Actually turn DIO back on in case there are more passes
	if (xgp->passes > 1) 
		p->target_options |= TO_DIO;

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"dio_before_io_operation: normal exit after reopen, p=0x%x\n",p);
} // End of xdd_dio_before_io_operation()

/*----------------------------------------------------------------------------*/
/* xdd_raw_before_io_operation(ptds_t *p) {
 */
void	// Read-After_Write Processing
xdd_raw_before_io_operation(ptds_t *p) {
	int	status;
	raw_t	*rawp;


	if (p->rawp == NULL)
		return;
	rawp = p->rawp;

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"raw_before_io_operation: enter, p=0x%x\n",p);

#if (IRIX || SOLARIS || AIX )
	struct stat64	statbuf;
#elif (LINUX || OSX || FREEBSD)
	struct stat	statbuf;
#endif

#if (LINUX || IRIX || SOLARIS || AIX || OSX || FREEBSD)
		if ((p->target_options & TO_READAFTERWRITE) && (p->target_options & TO_RAW_READER)) { 
// fprintf(stderr,"Reader: RAW check - dataready=%lld, trigger=%x\n",(long long)data_ready,p->raw_trigger);
			/* Check to see if we can read more data - if not see where we are at */
			if (rawp->raw_trigger & PTDS_RAW_STAT) { /* This section will continually poll the file status waiting for the size to increase so that it can read more data */
				while (rawp->raw_data_ready < p->iosize) {
					/* Stat the file so see if there is data to read */
#if (LINUX || OSX || FREEBSD)
					status = fstat(p->fd,&statbuf);
#else
					status = fstat64(p->fd,&statbuf);
#endif
					if (status < 0) {
						fprintf(xgp->errout,"%s: RAW: Error getting status on file\n", xgp->progname);
						rawp->raw_data_ready = p->iosize;
					} else { /* figure out how much more data we can read */
						rawp->raw_data_ready = statbuf.st_size - p->my_current_byte_location;
						if (rawp->raw_data_ready < 0) {
							/* The result of this should be positive, otherwise, the target file
							* somehow got smaller and there is a problem. 
							* So, fake it and let this loop exit 
							*/
							fprintf(xgp->errout,"%s: RAW: Something is terribly wrong with the size of the target file...\n",xgp->progname);
							rawp->raw_data_ready = p->iosize;
						}
					}
				}
			} else { /* This section uses a socket connection to the Destination and waits for the Source to tell it to receive something from its socket */
				while (rawp->raw_data_ready < p->iosize) {
					/* xdd_raw_read_wait() will block until there is data to read */
					status = xdd_raw_read_wait(p);
					if (rawp->raw_msg.length != p->iosize) 

						fprintf(stderr,"error on msg recvd %d loc %lld, length %lld\n",
							rawp->raw_msg_recv-1, 
							(long long)rawp->raw_msg.location,  
							(long long)rawp->raw_msg.length);
					if (rawp->raw_msg.sequence != rawp->raw_msg_last_sequence) {

						fprintf(stderr,"sequence error on msg recvd %d loc %lld, length %lld seq num is %lld should be %lld\n",
							rawp->raw_msg_recv-1, 
							(long long)rawp->raw_msg.location,  
							(long long)rawp->raw_msg.length, 
							rawp->raw_msg.sequence, 
							rawp->raw_msg_last_sequence);
					}
					if (rawp->raw_msg_last_sequence == 0) { /* this is the first message so prime the prev_loc and length with the current values */
						rawp->raw_prev_loc = rawp->raw_msg.location;
						rawp->raw_prev_len = 0;
					} else if (rawp->raw_msg.location <= rawp->raw_prev_loc) 
						/* this message is old and can be discgarded */
						continue;
					rawp->raw_msg_last_sequence++;
					/* calculate the amount of data to be read between the end of the last location and the end of the current one */
					rawp->raw_data_length = ((rawp->raw_msg.location + rawp->raw_msg.length) - (rawp->raw_prev_loc + rawp->raw_prev_len));
					rawp->raw_data_ready += rawp->raw_data_length;
					if (rawp->raw_data_length > p->iosize) 
						fprintf(stderr,"msgseq=%lld, loc=%lld, len=%lld, data_length is %lld, data_ready is now %lld, iosize=%d\n",
							rawp->raw_msg.sequence, 
							(long long)rawp->raw_msg.location, 
							(long long)rawp->raw_msg.length, 
							(long long)rawp->raw_data_length, 
							(long long)rawp->raw_data_ready, 
							p->iosize );
					rawp->raw_prev_loc = rawp->raw_msg.location;
					rawp->raw_prev_len = rawp->raw_data_length;
				}
			}
		} /* End of dealing with a read-after-write */
if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"raw_before_io_operation: exit, p=0x%x\n",p);
#endif
} // xdd_raw_before_io_operation()

/*----------------------------------------------------------------------------*/
/* xdd_e2e_before_io_operation() - This routine only does something if this
 * is the Destination side of an End-to-End operation.
 */
int32_t	
xdd_e2e_before_io_operation(ptds_t *p) {
	pclk_t	beg_time_tmp; 	// Beginning time of a data xfer
	pclk_t	end_time_tmp; 	// End time of a data xfer
	pclk_t	now; 	// End time of a data xfer
	int32_t	status;			// Status of subroutine calls
	e2e_t	*ep;


	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"e2e_before_io_operation: target_options=0x%016x\n",p->target_options);
	// If there is no end-to-end operation then just skip all this...
	if (!(p->target_options & TO_ENDTOEND)) 
		return(SUCCESS); 
	// We are the Source side - nothing to do - leave now...
	if (p->target_options & TO_E2E_SOURCE)
		return(SUCCESS);

	if (p->e2ep == NULL)
		return(SUCCESS);
	ep = p->e2ep;

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"e2e_before_io_operation: enter, p=0x%x\n",p);

	/* ------------------------------------------------------ */
	/* Start of destination's dealing with an End-to-End op   */
	/* This section uses a socket connection to the source    */
	/* to wait for data from the source, which it then writes */
	/* to the target file associated with this rarget 	  */
	/* ------------------------------------------------------ */
	// We are the Destination side of an End-to-End op
	if (xgp->global_options & GO_DEBUG) {
		fprintf(stderr,"e2e_before_io_operation: data_ready=%lld, current_op=%d,prev_loc=%lld, prev_len=%lld, iosize=%d\n",
			ep->e2e_data_ready, 
			p->my_current_op,
			ep->e2e_prev_loc, 
			ep->e2e_prev_len, 
			p->iosize);
	}

	// Lets read all the dta from the source 
	while (ep->e2e_data_ready < p->iosize) {
		/* xdd_e2e_dest_wait() will block until there is data to read */
		pclk_now(&beg_time_tmp);
		status = xdd_e2e_dest_wait(p);
		pclk_now(&end_time_tmp);
		ep->e2e_sr_time += (end_time_tmp - beg_time_tmp); // Time spent reading from the source machine
		// If status is "FAILED" then soemthing happened to the connection - time to leave
		if (status == FAILED) {
			fprintf(stderr,"%s: [mythreadnum %d]:e2e_before_io_operation: Connection closed prematurely by source!\n",
				xgp->progname,p->my_qthread_number);
			return(FAILED);
		}
			
		// Check the sequence number of the received msg to what we expect it to be
		// Also note that the msg magic number should not be MAGIQ (end of transmission)
		if ((ep->e2e_msg.sequence != ep->e2e_msg_last_sequence) && (ep->e2e_msg.magic != PTDS_E2E_MAGIQ )) {
			fprintf(stderr,"%s: [my_qthread_number %d]:sequence error on msg recvd %d loc %lld, length %lld seq num is %lld should be %lld\n",
				xgp->progname,
				p->my_qthread_number, 
				ep->e2e_msg_recv-1, 
				ep->e2e_msg.location,  
				ep->e2e_msg.length, 
				ep->e2e_msg.sequence, 
				ep->e2e_msg_last_sequence);
			return(FAILED);
		}

		// Check to see of this is the last message in the transmission
		// If so, then set the "my_pass_ring" so that we exit gracefully
		if (ep->e2e_msg.magic == PTDS_E2E_MAGIQ) 
			p->my_pass_ring = 1;

		// Check for a timeout condition - this only happens if we are using UDP
		if (ep->e2e_timedout) {
			fprintf(stderr,"%s: [mythreadnum %d]:timedout...go on to next pass or quit if last pass\n", 
			xgp->progname,
			p->mythreadnum);
			pclk_now(&now);
			return(FAILED);
		}
		// Display some useful information if we are debugging this thing
		if (xgp->global_options & GO_DEBUG) {
			fprintf(stderr, "[mythreadnum %d]:e2e_before_io_operation: XXXXXXXX  msg.sequence=%lld, msg.location=%lld, msg.length=%lld, msg_last_sequence=%lld\n",
				p->mythreadnum, ep->e2e_msg.sequence, ep->e2e_msg.location, ep->e2e_msg.length, ep->e2e_msg_last_sequence);
			fprintf(stderr, "[mythreadnum %d]:e2e_before_io_operation: XXXXXXXX  data_length=%lld, data_ready=%lld, iosize=%d\n",
				p->mythreadnum, ep->e2e_data_length, ep->e2e_data_ready, p->iosize );
			fprintf(stderr, "[mythreadnum %d]:e2e_before_io_operation: XXXXXXXX  prev_loc=%lld, prev_len=%lld\n",
				p->mythreadnum, ep->e2e_prev_loc, ep->e2e_prev_len );
		}
		// Check to see which message we are on and set up the msg counters properly
		if (ep->e2e_msg_last_sequence == 0) { 
			// This is the first message so prime the prev_loc and length with the current values 
			ep->e2e_prev_loc = ep->e2e_msg.location;
			ep->e2e_prev_len = 0;
		} else if (ep->e2e_msg.location <= ep->e2e_prev_loc) {
			fprintf(stderr,"[mythreadnum %d]:e2e_before_io_operation: OLD MESSAGE\n", p->mythreadnum); 
			// This message is old and can be discgarded 
			continue;
		}

		// The e2e_msg_last_sequence variable is the value of what we think e2e_msg.sequence
		//   should be in the incoming msg
		ep->e2e_msg_last_sequence++;

		// Calculate the amount of data to be read between the end of 
		//   the last location and the end of the current one 
		ep->e2e_data_length = ep->e2e_msg.length;
		ep->e2e_data_ready += ep->e2e_data_length;
		ep->e2e_prev_loc = ep->e2e_msg.location;
		ep->e2e_prev_len = ep->e2e_data_length;

		// If this is the last message and the length of the data in the message is 
		// less than iosize then we can just exit the loop now because this will be a short write
		if ((p->my_current_op == (p->target_ops - 1)) &&
		    (ep->e2e_msg.length < p->iosize)) {
			p->iosize = ep->e2e_msg.length;
			break;
		}
	}  // End of WHILE loop that processes an End-to-End test

	// For End-to-End, set the relative location of this data to where the SOURCE 
	//  machine thinks it should be.
	p->my_current_byte_location = ep->e2e_msg.location;

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"e2e_before_io_operation: exit, p=0x%x\n",p);
	return(SUCCESS);

} // xdd_e2e_before_io_operation()

/*----------------------------------------------------------------------------*/
/* xdd_ts_before_io_operation() - This routine will resord information in the
 * timestamp table if needed.
 */
void	
xdd_ts_before_io_operation(ptds_t *p) {
	pclk_t	tt;	// Temp Time
	timestamp_t	*tsp;
	int64_t		indx;



	if (p->tsp == NULL) 
		return;
	tsp = p->tsp;

	/* If time stamping is no on then just return */
	if ((tsp->ts_options & TS_ON) == 0)
		return;

	/* We record information only if the trigger time or operation has been reached or if we record all */
	pclk_now(&tt);
	if ((tsp->ts_options & TS_TRIGGERED) || 
	    (tsp->ts_options & TS_ALL) ||
	    ((tsp->ts_options & TS_TRIGTIME) && (tt >= tsp->ts_trigtime)) ||
	    ((tsp->ts_options & TS_TRIGOP) && (tsp->ts_trigop == p->my_current_op))) {
		tsp->ts_options |= TS_TRIGGERED;
		indx = tsp->ttp->tte_indx;
		tsp->ttp->tte[indx].rwvop = p->seekhdr.seeks[p->my_current_op].operation;
		tsp->ttp->tte[indx].pass = p->my_current_pass_number;
		tsp->ttp->tte[indx].byte_location = p->my_current_byte_location;
		tsp->ttp->tte[indx].opnumber = p->my_current_op;
		tsp->ttp->tte[indx].start = tt;
		tsp->timestamps++;
	}
} // xdd_ts_before_io_operation()

/*----------------------------------------------------------------------------*/
/* xdd_throttle_before_io_operation() - This routine implements the throttling
 * mechanism which is essentially a delay before the next I/O operation such
 * that the overall bandwdith or IOP rate meets the throttled value.
 */
void	
xdd_throttle_before_io_operation(ptds_t *p) {
	pclk_t   sleep_time;         /* This is the number of pico seconds to sleep between I/O ops */
	int32_t  sleep_time_dw;     /* This is the amount of time to sleep in milliseconds */
	pclk_t	now;


	if (p->throttle <= 0.0)
		return;

	/* If this is a 'throttled' operation, check to see what time it is relative to the start
	 * of this pass, compare that to the time that this operation was supposed to begin, and
	 * go to sleep for how ever many milliseconds is necessary until the next I/O needs to be
	 * issued. If we are past the issue time for this operation, just issue the operation.
	 */
	if (p->throttle > 0.0) {
		pclk_now(&now);
		if (p->throttle_type & PTDS_THROTTLE_DELAY) {
			sleep_time = p->throttle*1000000;
			fprintf(stderr,"throttle delay for %d seconds\n",sleep_time);
		} else { // Process the throttle for IOPS or BW
			now -= p->my_pass_start_time;
			if (now < p->seekhdr.seeks[p->my_current_op].time1) { /* Then we may need to sleep */
				sleep_time = (p->seekhdr.seeks[p->my_current_op].time1 - now) / BILLION; /* sleep time in milliseconds */
				if (sleep_time > 0) {
					sleep_time_dw = sleep_time;
#ifdef WIN32
					Sleep(sleep_time_dw);
#elif (LINUX || IRIX || AIX || OSX || FREEBSD) /* Change this line to use usleep */
					if ((sleep_time_dw*CLK_TCK) > 1000) /* only sleep if it will be 1 or more ticks */
#if (IRIX )
						sginap((sleep_time_dw*CLK_TCK)/1000);
#elif (LINUX || AIX || OSX || FREEBSD) /* Change this line to use usleep */
fprintf(stderr,"throttle sleeping for %d seconds\n",sleep_time_dw*1000);
						usleep(sleep_time_dw*1000);
#endif
#endif
				}
			}
		}
	}
} // xdd_throttle_before_io_operation()

/*----------------------------------------------------------------------------*/
/* xdd_io_loop_before_io_operation() - This subroutine will do all the stuff 
 * needed to be done before an I/O operation is issued.
 * This routine is called within the inner I/O loop before every I/O.
 */
int32_t
xdd_io_loop_before_io_operation(ptds_t *p) {
	int32_t	status;	// Return status from various subroutines

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"io_loop_before_io_operation: enter, p=0x%x, op=%lld\n",p,p->my_current_op);

	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"before_io_operation: calling syncio barrier\n");
	// Syncio barrier - wait for all others to get here 
	xdd_syncio_before_io_operation(p);

	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"before_io_operation: calling start trigger\n");
	// Check to see if we need to wait for another target to trigger us to start.
	xdd_start_trigger_before_io_operation(p);

	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"before_io_operation: calling lockstep\n");
	// Lock Step Processing
	status = xdd_lockstep_before_io_operation(p);
	if (status) return(FAILED);

	/* init the error number and break flag for good luck */
	errno = 0;
	p->my_error_break = 0;
	/* Get the location to seek to */
	if (p->seekhdr.seek_options & SO_SEEK_NONE) /* reseek to starting offset if noseek is set */
		p->my_current_byte_location = (uint64_t)((p->my_target_number * xgp->target_offset) + 
											p->seekhdr.seeks[0].block_location) * 
											p->block_size;
	else p->my_current_byte_location = (uint64_t)((p->my_target_number * xgp->target_offset) + 
											p->seekhdr.seeks[p->my_current_op].block_location) * 
											p->block_size;

	// DirectIO Handling
	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"before_io_operation: calling dio\n");
	xdd_dio_before_io_operation(p);

	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"before_io_operation: calling raw\n");
	// Read-After_Write Processing
	xdd_raw_before_io_operation(p);

	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"before_io_operation: calling e2e\n");
	// End-to-End Processing
	status = xdd_e2e_before_io_operation(p);
	if (status == FAILED) {
		fprintf(xgp->errout,"%s: [mythreadnum %d]: io_loop_before_io_operation: Requesting termination due to previous error.\n",
			xgp->progname, p->mythreadnum);
		xgp->abort_io;
		return(FAILED);
	}

	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"before_io_operation: calling timestamp\n");
	// Time Stamp Processing
	xdd_ts_before_io_operation(p);

	if (xgp->global_options & GO_DEBUG) 
		fprintf(stderr,"before_io_operation: calling throttle\n");
	// Throttle Processing
	xdd_throttle_before_io_operation(p);

if (xgp->global_options & GO_DEBUG_INIT) fprintf(stderr,"io_loop_before_io_operation: exit, p=0x%x\n",p);

	return(SUCCESS);

} // End of xdd_io_loop_before_io_operation()

 
