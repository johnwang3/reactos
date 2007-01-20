/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/fsrtl/fastio.c
 * PURPOSE:         Provides Fast I/O entrypoints to the Cache Manager
 * PROGRAMMERS:     None.
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <ntifs.h>
#include <cctypes.h>

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
VOID
NTAPI
FsRtlIncrementCcFastReadResourceMiss(VOID)
{
    CcFastReadResourceMiss++;
}

/*
 * @implemented
 */
VOID 
NTAPI
FsRtlIncrementCcFastReadNotPossible(VOID)
{
    CcFastReadNotPossible++;
}

/*
 * @implemented
 */
VOID
NTAPI
FsRtlIncrementCcFastReadWait(VOID)
{
    CcFastReadWait++;
}

/*
 * @implemented
 */
VOID
NTAPI
FsRtlIncrementCcFastReadNoWait(VOID)
{
    CcFastReadNoWait++;
}

_SEH_FILTER(FsRtlCcCopyFilter)
{
   LONG ExceptionDisposition;
 
   /* Check if this was an expected exception */
   ExceptionDisposition = FsRtlIsNtstatusExpected(_SEH_GetExceptionCode() ?
                                                  EXCEPTION_EXECUTE_HANDLER :
                                                  EXCEPTION_CONTINUE_SEARCH);
 
   /* Continue execution if we expected it, otherwise fail the call */
   return ExceptionDisposition;
}	


BOOLEAN
NTAPI
FsRtlCopyRead(IN PFILE_OBJECT FileObject,
              IN PLARGE_INTEGER FileOffset,
              IN ULONG Length,
              IN BOOLEAN Wait,
              IN ULONG LockKey,
              OUT PVOID Buffer,
              OUT PIO_STATUS_BLOCK IoStatus,
              IN PDEVICE_OBJECT DeviceObject)
{

    PFSRTL_COMMON_FCB_HEADER FcbHeader;
    LARGE_INTEGER Offset;
    PFAST_IO_DISPATCH FastIoDispatch;
    PDEVICE_OBJECT Device;
    BOOLEAN Result = TRUE;
	ULONG PageCount = COMPUTE_PAGES_SPANNED(FileOffset,Length);

	PAGED_CODE();
    ASSERT(FileObject);
    ASSERT(FileObject->FsContext);

    /* No actual read */
    if (!Length)
    {
        /* Return success */
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = 0;
        return TRUE;
    }
		
    if (MAXLONGLONG < (LONGLONG) FileOffset->QuadPart + Length) {
			IoStatus->Status = STATUS_INVALID_PARAMETER;
			IoStatus->Information = 0;
			return FALSE;
    }

    /* Get the offset and FCB header */
    Offset.QuadPart = FileOffset->QuadPart + Length;
    FcbHeader = (PFSRTL_COMMON_FCB_HEADER)FileObject->FsContext;

    
	if (Wait) {
        /* Use a Resource Acquire */
        FsRtlEnterFileSystem();
		CcFastReadWait++;
        ExAcquireResourceSharedLite(FcbHeader->Resource, TRUE);
	} else {
		/* Acquire the resource without blocking */
		/* Return false and the I/O manager will retry using the standard IRP method. */
        /* Use a Resource Acquire */
        FsRtlEnterFileSystem();
        if (!ExAcquireResourceSharedLite(FcbHeader->Resource, FALSE)) {
        	FsRtlExitFileSystem();
			FsRtlIncrementCcFastReadResourceMiss();
			return FALSE;
        }
    }


    /* Check if this is a fast I/O cached file */
    if (!(FileObject->PrivateCacheMap) ||
        (FcbHeader->IsFastIoPossible == FastIoIsNotPossible)) {
        /* It's not, so fail */
		Result = FALSE;
       goto Cleanup;
    }

    /* Check if we need to find out if fast I/O is available */
    if (FcbHeader->IsFastIoPossible == FastIoIsQuestionable)
    {
        /* Sanity check */
        ASSERT(!KeIsExecutingDpc());

        /* Get the Fast I/O table */
        Device = IoGetRelatedDeviceObject(FileObject);
	     FastIoDispatch = Device->DriverObject->FastIoDispatch;

        /* Sanity check */
        ASSERT(FastIoDispatch != NULL);
        ASSERT(FastIoDispatch->FastIoCheckIfPossible != NULL);

        /* Ask the driver if we can do it */
        if (!FastIoDispatch->FastIoCheckIfPossible(FileObject,
                                                   FileOffset,
                                                   Length,
                                                   TRUE,
                                                   LockKey,
                                                   TRUE,
                                                   IoStatus,
                                                   Device))
        {
            /* It's not, fail */
            Result = FALSE;
            goto Cleanup;
        }
	}

    /* Check if we read too much */
    if (Offset.QuadPart > FcbHeader->FileSize.QuadPart){
        /* We did, check if the file offset is past the end */
        if (FileOffset->QuadPart >= FcbHeader->FileSize.QuadPart){
            /* Set end of file */
            IoStatus->Status = STATUS_END_OF_FILE;
            IoStatus->Information = 0;
            goto Cleanup;
        }

        /* Otherwise, just normalize the length */
        Length = (ULONG)(FcbHeader->FileSize.QuadPart - FileOffset->QuadPart);
    }

    /* Set this as top-level IRP */ 
   	PsGetCurrentThread()->TopLevelIrp = FSRTL_FAST_IO_TOP_LEVEL_IRP;

	_SEH_TRY {
	    /* Make sure the IO and file size is below 4GB */
		if (Wait && !(Offset.HighPart | FcbHeader->FileSize.HighPart )) {
				
                /* Call the cache controller */
				CcFastCopyRead (FileObject,FileOffset->LowPart,Length,PageCount,Buffer,IoStatus);
                /* File was accessed */
				FileObject->Flags |= FO_FILE_FAST_IO_READ;
				if (IoStatus->Status != STATUS_END_OF_FILE) {
					ASSERT(( FcbHeader->FileSize.QuadPart) >= (FileOffset->QuadPart + IoStatus->Information));
				}
				
		} else {
		
            /* Call the cache controller */
			Result = CcCopyRead(FileObject, FileOffset, Length, Wait,Buffer, IoStatus);
            /* File was accessed */
			FileObject->Flags |= FO_FILE_FAST_IO_READ;
			if (Result == TRUE) {
				ASSERT(		(IoStatus->Status == STATUS_END_OF_FILE) ||	
									((LONGLONG)(FileOffset->QuadPart + IoStatus->Information) <= FcbHeader->FileSize.QuadPart)
							  );
			}
		}

        /* Update the current file offset */
		if (Result == TRUE) {
			FileObject->CurrentByteOffset.QuadPart += IoStatus->Information;
		}
	}
	_SEH_EXCEPT(FsRtlCcCopyFilter) {
		Result = FALSE;
       } _SEH_END;
	
	PsGetCurrentThread()->TopLevelIrp = 0;
	
    /* Return to caller */
Cleanup:

    ExReleaseResourceLite(FcbHeader->Resource);
    FsRtlExitFileSystem();

	if (Result == FALSE) {
        CcFastReadNotPossible += 1;
	}

    return Result;
    
}


BOOLEAN
NTAPI
FsRtlCopyWrite(IN PFILE_OBJECT FileObject,
               IN PLARGE_INTEGER FileOffset,
               IN ULONG Length,
               IN BOOLEAN Wait,
               IN ULONG LockKey,
               OUT PVOID Buffer,
               OUT PIO_STATUS_BLOCK IoStatus,
               IN PDEVICE_OBJECT DeviceObject)
{

    BOOLEAN Result = TRUE;
    PFAST_IO_DISPATCH FastIoDispatch;
    PDEVICE_OBJECT Device;
    PFSRTL_COMMON_FCB_HEADER FcbHeader;

    /* WDK doc. Offset=0xffffffffffffffff indicates append to the end of file */
    BOOLEAN FileOffsetAppend = ((FileOffset->HighPart == 0xffffffff) && (FileOffset->LowPart == 0xffffffff));
    BOOLEAN ResourceAquiredShared = FALSE;

    BOOLEAN b_4GB = FALSE;

    BOOLEAN FileSizeModified = FALSE;
    LARGE_INTEGER OldFileSize;
    LARGE_INTEGER OldValidDataLength;

    LARGE_INTEGER NewSize;
    LARGE_INTEGER Offset;
    
    PAGED_CODE();

    ASSERT(FileObject);
    ASSERT(FileObject->FsContext);

    /* Initialize some of the vars and pointers */
    NewSize.QuadPart = 0;
    Offset.QuadPart = FileOffset->QuadPart + Length;
    FcbHeader = (PFSRTL_COMMON_FCB_HEADER)FileObject->FsContext;

    /* Nagar p.544 -- Check with Cc if we can write and check if the IO > 64kB (WDK macro) */
    if ( (CcCanIWrite(FileObject, Length,Wait, FALSE) == FALSE) ||
          (CcCopyWriteWontFlush(FileObject,FileOffset,Length) == FALSE) ||
          ((FileObject->Flags & FO_WRITE_THROUGH )== TRUE)
    	)
    {
    	return FALSE;
    }

    /* No actual read */
    if (!Length)
    {
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = Length;
        return TRUE;
    }

    FsRtlEnterFileSystem();

    /* Nagar p.544/545 -- The CcFastCopyWrite doesn't deal with filesize beyond 4GB*/
    if (Wait && (FcbHeader->AllocationSize.HighPart == 0)) 
    {
        /* If the file offset is not past the file size, then we can acquire the lock shared */
        if ((FileOffsetAppend == FALSE) && (Offset.LowPart <= FcbHeader->ValidDataLength.LowPart)){
        	ExAcquireResourceSharedLite(FcbHeader->Resource,TRUE);
        	ResourceAquiredShared = TRUE;
        } else {
    		ExAcquireResourceExclusiveLite(FcbHeader->Resource,TRUE);
        }

        /* Nagar p.544/545 -- If we append, use the file size as offset. Also, check that we aren't crossing the 4GB boundary */
        if ((FileOffsetAppend == TRUE)) {
        	Offset.LowPart = FcbHeader->FileSize.LowPart;
        	NewSize.LowPart = FcbHeader->FileSize.LowPart + Length;
        	b_4GB = (NewSize.LowPart < FcbHeader->FileSize.LowPart);
        	
        } else {
        	Offset.LowPart = FileOffset->LowPart; 
        	NewSize.LowPart = FileOffset->LowPart + Length;
        	b_4GB = ((NewSize.LowPart < FileOffset->LowPart) || (FileOffset->HighPart != 0));
        }

        /*  Nagar p.544/545
            Make sure that caching is initated.
            That fast are allowed for this file stream.
            That we are not extending past the allocated size
            That we are not creating a hole bigger than 8k
            That we are not crossing the 4GB boundary            
        */
        if ( 	(FileObject->PrivateCacheMap != NULL)  &&
        		(FcbHeader->IsFastIoPossible != FastIoIsNotPossible) &&
        		(FcbHeader->AllocationSize.LowPart >= NewSize.LowPart) &&
        		(Offset.LowPart < (FcbHeader->ValidDataLength.LowPart + 0x2000) ) &&
        		!b_4GB
        	)
        {
            /*  If we are extending past the file size, we need to release the lock and acquire it
                exclusively, because we are going to need to update the FcbHeader */
        	if (ResourceAquiredShared && (NewSize.LowPart  > (FcbHeader->ValidDataLength.LowPart + 0x2000))) {
        		/* Then we need to acquire the resource exclusive */
        		ExReleaseResourceLite(FcbHeader->Resource); 
        		ExAcquireResourceExclusiveLite(FcbHeader->Resource,TRUE);
        		if (FileOffsetAppend == TRUE) {
        			Offset.LowPart = FcbHeader->FileSize.LowPart; // ??
        			NewSize.LowPart = FcbHeader->FileSize.LowPart + Length;
        			/* Make sure we don't cross the 4GB boundary */
        			b_4GB = (NewSize.LowPart < Offset.LowPart);
        		} 
        		
                /* Recheck some of the conditions since we let the lock go */
        		if ( 	(FileObject->PrivateCacheMap != NULL)  &&
        				(FcbHeader->IsFastIoPossible != FastIoIsNotPossible) &&
        				(FcbHeader->AllocationSize.LowPart >= NewSize.LowPart) &&
        				(FcbHeader->AllocationSize.HighPart == 0) &&
        				!b_4GB
        			) {
        		} else {
        			goto FailAndCleanup;
        		}
        	}
        		
        }else {
        		goto FailAndCleanup;
        }

	    /* Check if we need to find out if fast I/O is available */
	    if (FcbHeader->IsFastIoPossible == FastIoIsQuestionable)
	    {
			 IO_STATUS_BLOCK FastIoCheckIfPossibleStatus;
	    
	        /* Sanity check */
	        ASSERT(!KeIsExecutingDpc());

	        /* Get the Fast I/O table */
	        Device = IoGetRelatedDeviceObject(FileObject);
		     FastIoDispatch = Device->DriverObject->FastIoDispatch;

	        /* Sanity check */
	        ASSERT(FastIoDispatch != NULL);
	        ASSERT(FastIoDispatch->FastIoCheckIfPossible != NULL);

	        /* Ask the driver if we can do it */
	        if (!FastIoDispatch->FastIoCheckIfPossible(FileObject,
	                                                   FileOffsetAppend? &FcbHeader->FileSize:FileOffset,
	                                                   Length,
	                                                   TRUE,
	                                                   LockKey,
	                                                   FALSE,
	                                                   &FastIoCheckIfPossibleStatus,
	                                                   Device))
	        {
	            /* It's not, fail */
	            goto FailAndCleanup;
	        }
		}
			
        /*  If we are going to extend the file then save the old file size
            in case the operation fails
        */
		if (NewSize.LowPart > FcbHeader->FileSize.LowPart) {
			FileSizeModified = TRUE;
			OldFileSize.LowPart = FcbHeader->FileSize.LowPart;
			OldValidDataLength.LowPart = FcbHeader->ValidDataLength.LowPart;
			FcbHeader->FileSize.LowPart = NewSize.LowPart;
		}
			
	    /* Set this as top-level IRP */ 
	    PsGetCurrentThread()->TopLevelIrp = FSRTL_FAST_IO_TOP_LEVEL_IRP;

		_SEH_TRY {
			if (Offset.LowPart > FcbHeader->ValidDataLength.LowPart) {
				LARGE_INTEGER OffsetVar;
				OffsetVar.LowPart = Offset.LowPart;
				OffsetVar.HighPart = 0;
				CcZeroData(FileObject,&FcbHeader->ValidDataLength,&OffsetVar,TRUE);
			}

			/* Call the cache manager */
			CcFastCopyWrite(FileObject,Offset.LowPart,Length,Buffer);
		}	
		_SEH_EXCEPT(FsRtlCcCopyFilter) {
			Result = FALSE;
	    } _SEH_END;
				
        /* Remove ourselves at the top level component after the IO is done */
	    PsGetCurrentThread()->TopLevelIrp = 0;

        /* Did the operation succeed ? */
	    if (Result == TRUE) {
	        /* Update the valid file size if necessary */
			if (NewSize.LowPart > FcbHeader->ValidDataLength.LowPart){
				FcbHeader->ValidDataLength.LowPart = NewSize.LowPart ;
			}

            /* Flag the file as modified */
			FileObject->Flags |= FO_FILE_MODIFIED;

			/* Update the strucutres if the file size changed */
			if (FileSizeModified) {
				((SHARED_CACHE_MAP*) FileObject->SectionObjectPointer->SharedCacheMap)->FileSize.LowPart = NewSize.LowPart;
				FileObject->Flags |= FO_FILE_SIZE_CHANGED;
			}

            /* Update the file object current file offset */
			FileObject->CurrentByteOffset.QuadPart = NewSize.LowPart;
			
		} else {
		
			/* Result == FALSE if we get here. */
			if (FileSizeModified) {
			    /* If the file size was modified then restore the old file size */
				if(FcbHeader->PagingIoResource != NULL) {
					// Nagar P.544 Restore the old file size if operation didn't succeed.
					ExAcquireResourceExclusiveLite(FcbHeader->PagingIoResource,TRUE);
					FcbHeader->FileSize.LowPart = OldFileSize.LowPart;
					FcbHeader->ValidDataLength.LowPart = OldValidDataLength.LowPart;
					ExReleaseResourceLite(FcbHeader->PagingIoResource);
				} else {
				    /* If there is no lock and do it without */
					FcbHeader->FileSize.LowPart = OldFileSize.LowPart;
					FcbHeader->ValidDataLength.LowPart = OldValidDataLength.LowPart;
				}
			} else {
			}
		}
		
		goto Cleanup;
		
	} else {
		
		LARGE_INTEGER OldFileSize;

		/* Sanity check */
		ASSERT(!KeIsExecutingDpc());
		
		// Nagar P.544 
        /* Check if we need to acquire the resource exclusive */		
		if ( 	(FileOffsetAppend == FALSE) &&
				( (FileOffset->QuadPart + Length)  <= FcbHeader->ValidDataLength.QuadPart )
			)
		{
		    /* Acquire the resource shared */
			if (!ExAcquireResourceSharedLite(FcbHeader->Resource,Wait)) {
				goto FailAndCleanup;
			}
			ResourceAquiredShared =TRUE;
		} else {
		    /* Acquire the resource exclusive */
			if (!ExAcquireResourceExclusiveLite(FcbHeader->Resource,Wait)) {
				goto FailAndCleanup;
			}
		}

		/* Check if we are appending */
		if (FileOffsetAppend == TRUE) {
			Offset.QuadPart = FcbHeader->FileSize.QuadPart;
			NewSize.QuadPart = FcbHeader->FileSize.QuadPart + Length;
		} else {
			Offset.QuadPart = FileOffset->QuadPart;
			NewSize.QuadPart += FileOffset->QuadPart + Length;
		}

        /*  Nagar p.544/545
            Make sure that caching is initated.
            That fast are allowed for this file stream.
            That we are not extending past the allocated size
            That we are not creating a hole bigger than 8k
        */
		if ( 	(FileObject->PrivateCacheMap != NULL)  &&
				(FcbHeader->IsFastIoPossible != FastIoIsNotPossible) &&
				((FcbHeader->ValidDataLength.QuadPart + 0x2000) > Offset.QuadPart) &&
				(MAXLONGLONG > (Offset.QuadPart + Length)) &&
				(FcbHeader->AllocationSize.QuadPart >= NewSize.QuadPart)
			) 
		{
		    /* Check if we can keep the lock shared */
			if (ResourceAquiredShared && (NewSize.QuadPart > FcbHeader->ValidDataLength.QuadPart) ) {
				ExReleaseResourceLite(FcbHeader->Resource);
				if(!ExAcquireResourceExclusiveLite(FcbHeader->Resource,Wait)) {
					goto LeaveCriticalAndFail;
				}

                /* Compute the offset and the new filesize */
				if (FileOffsetAppend) {
					Offset.QuadPart = FcbHeader->FileSize.QuadPart;
					NewSize.QuadPart = FcbHeader->FileSize.QuadPart + Length;
				}

                /* Recheck the above points since we released and reacquire the lock   */
				if ( 	(FileObject->PrivateCacheMap != NULL)  &&
						(FcbHeader->IsFastIoPossible != FastIoIsNotPossible) &&
						(FcbHeader->AllocationSize.QuadPart > NewSize.QuadPart)
					) {
					/* Do nothing */
				} else {
					goto FailAndCleanup;
				}
			}

		    /* Check if we need to find out if fast I/O is available */
		    if (FcbHeader->IsFastIoPossible == FastIoIsQuestionable)
		    {
				 IO_STATUS_BLOCK FastIoCheckIfPossibleStatus;
		    
		        /* Sanity check */
		        ASSERT(!KeIsExecutingDpc());

		        /* Get the Fast I/O table */
		        Device = IoGetRelatedDeviceObject(FileObject);
			     FastIoDispatch = Device->DriverObject->FastIoDispatch;

		        /* Sanity check */
		        ASSERT(FastIoDispatch != NULL);
		        ASSERT(FastIoDispatch->FastIoCheckIfPossible != NULL);

		        /* Ask the driver if we can do it */
		        if (!FastIoDispatch->FastIoCheckIfPossible(FileObject,
		                                                   FileOffsetAppend? &FcbHeader->FileSize:FileOffset,
		                                                   Length,
		                                                   TRUE,
		                                                   LockKey,
		                                                   FALSE,
		                                                   &FastIoCheckIfPossibleStatus,
		                                                   Device))
		        {
		            /* It's not, fail */
		            goto FailAndCleanup;
		        }
			}


            /* If we are going to modify the filesize, save the old fs in case the operation fails */            
			if (NewSize.QuadPart > FcbHeader->FileSize.QuadPart) {
				FileSizeModified = TRUE;
				OldFileSize.QuadPart = FcbHeader->FileSize.QuadPart;
				OldValidDataLength.QuadPart = FcbHeader->ValidDataLength.QuadPart;

				/* If the high part of the filesize is going to change, grab the Paging IoResouce */
				if (NewSize.HighPart != FcbHeader->FileSize.HighPart && FcbHeader->PagingIoResource) {
					ExAcquireResourceExclusiveLite(FcbHeader->PagingIoResource, TRUE);
					FcbHeader->FileSize.QuadPart = NewSize.QuadPart;
					ExReleaseResourceLite(FcbHeader->PagingIoResource);
				} else {
					FcbHeader->FileSize.QuadPart = NewSize.QuadPart;
				}
			}

            /* Nagar p.544 */
            /* Set ourselves as top component */
		    PsGetCurrentThread()->TopLevelIrp = FSRTL_FAST_IO_TOP_LEVEL_IRP;
			_SEH_TRY {
   				BOOLEAN CallCc = TRUE;
                /*  Check if there is a gap between the end of the file and the offset
                    If yes, then we have to zero the data
                */
				if (Offset.QuadPart > FcbHeader->ValidDataLength.QuadPart) {
					if (!(Result = CcZeroData(FileObject,&FcbHeader->ValidDataLength,&Offset,Wait))) {
					    /*  If this operation fails, then we have to exit
					        We can jump outside the SEH, so I a using a variable to exit
					        normally
					    */
						CallCc = FALSE;
					} 
				}

                /* Unless the CcZeroData failed, call the cache manager */
				if (CallCc) {
					Result = CcCopyWrite(FileObject,&Offset,Length, Wait, Buffer);
				}
			}_SEH_EXCEPT(FsRtlCcCopyFilter) {
				Result = FALSE;
		    } _SEH_END;
		
            /* Reset the top component */
		    PsGetCurrentThread()->TopLevelIrp = FSRTL_FAST_IO_TOP_LEVEL_IRP;

            /* Did the operation suceeded */
			if (Result) {
			    /* Check if we need to update the filesize */
				if (NewSize.QuadPart > FcbHeader->ValidDataLength.QuadPart) {
					if (NewSize.HighPart != FcbHeader->ValidDataLength.HighPart && FcbHeader->PagingIoResource) {
						ExAcquireResourceExclusiveLite(FcbHeader->PagingIoResource, TRUE);
						FcbHeader->ValidDataLength.QuadPart = NewSize.QuadPart;
						ExReleaseResourceLite(FcbHeader->PagingIoResource);
					} else {
						FcbHeader->ValidDataLength.QuadPart = NewSize.QuadPart;
					}

                    /* Flag the file as modified */					
					FileObject->Flags |= FO_FILE_MODIFIED;
					/* Check if the filesize has changed */
					if (FileSizeModified) {
					    /* Update the file sizes */
						((SHARED_CACHE_MAP*) FileObject->SectionObjectPointer->SharedCacheMap)->FileSize.QuadPart = NewSize.QuadPart;
						FileObject->Flags |= FO_FILE_SIZE_CHANGED;
					}
					/* Update the current FO byte offset */
					FileObject->CurrentByteOffset.QuadPart = NewSize.QuadPart;
				}
			}
			else
			{
			    /*  The operation did not succeed
			        Reset the file size to what it should be
			    */
				if (FileSizeModified) {
					if (FcbHeader->PagingIoResource) {
						ExAcquireResourceExclusiveLite(FcbHeader->PagingIoResource, TRUE);
						FcbHeader->FileSize.QuadPart = OldFileSize.QuadPart;
						FcbHeader->ValidDataLength.QuadPart = OldValidDataLength.QuadPart;
						ExReleaseResourceLite(FcbHeader->PagingIoResource);
					}else{
						FcbHeader->FileSize.QuadPart = OldFileSize.QuadPart;
						FcbHeader->ValidDataLength.QuadPart = OldValidDataLength.QuadPart;
					}
				}
			}
			goto Cleanup;
		} else {
			goto FailAndCleanup;
		}

		ASSERT(0);
	}

LeaveCriticalAndFail:
    FsRtlExitFileSystem();
    return FALSE;


FailAndCleanup:

    ExReleaseResourceLite(FcbHeader->Resource);
    FsRtlExitFileSystem();
    return FALSE;

Cleanup:

    ExReleaseResourceLite(FcbHeader->Resource);
    FsRtlExitFileSystem();
    return Result;
}   

/*
 * @implemented
 */
NTSTATUS
NTAPI
FsRtlGetFileSize(IN PFILE_OBJECT  FileObject,
                 IN OUT PLARGE_INTEGER FileSize)
{
    KEBUGCHECK(0);
    return STATUS_UNSUCCESSFUL;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
FsRtlMdlRead(IN PFILE_OBJECT FileObject,
             IN PLARGE_INTEGER FileOffset,
             IN ULONG Length,
             IN ULONG LockKey,
             OUT PMDL *MdlChain,
             OUT PIO_STATUS_BLOCK IoStatus)
{
    KEBUGCHECK(0);
    return FALSE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
FsRtlMdlReadComplete(IN PFILE_OBJECT FileObject,
                     IN OUT PMDL MdlChain)
{
    KEBUGCHECK(0);
    return FALSE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
FsRtlMdlReadCompleteDev(IN PFILE_OBJECT FileObject,
                        IN PMDL MdlChain,
                        IN PDEVICE_OBJECT DeviceObject)
{
    KEBUGCHECK(0);
    return FALSE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
FsRtlMdlReadDev(IN PFILE_OBJECT FileObject,
                IN PLARGE_INTEGER FileOffset,
                IN ULONG Length,
                IN ULONG LockKey,
                OUT PMDL *MdlChain,
                OUT PIO_STATUS_BLOCK IoStatus,
                IN PDEVICE_OBJECT DeviceObject)
{
    KEBUGCHECK(0);
    return FALSE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
FsRtlMdlWriteComplete(IN PFILE_OBJECT FileObject,
                      IN PLARGE_INTEGER FileOffset,
                      IN PMDL MdlChain)
{
    KEBUGCHECK(0);
    return FALSE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
FsRtlMdlWriteCompleteDev(IN PFILE_OBJECT FileObject,
                         IN PLARGE_INTEGER FileOffset,
                         IN PMDL MdlChain,
                         IN PDEVICE_OBJECT DeviceObject)
{
    KEBUGCHECK(0);
    return FALSE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
FsRtlPrepareMdlWrite(IN PFILE_OBJECT FileObject,
                     IN PLARGE_INTEGER FileOffset,
                     IN ULONG Length,
                     IN ULONG LockKey,
                     OUT PMDL *MdlChain,
                     OUT PIO_STATUS_BLOCK IoStatus)
{
    KEBUGCHECK(0);
    return FALSE;
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
FsRtlPrepareMdlWriteDev(IN PFILE_OBJECT FileObject,
                        IN PLARGE_INTEGER FileOffset,
                        IN ULONG Length,
                        IN ULONG LockKey,
                        OUT PMDL *MdlChain,
                        OUT PIO_STATUS_BLOCK IoStatus,
                        IN PDEVICE_OBJECT DeviceObject)
{
    KEBUGCHECK(0);
    return FALSE;
}

/*
* @implemented
*/
VOID
NTAPI
FsRtlAcquireFileExclusive(IN PFILE_OBJECT FileObject)
{
    KEBUGCHECK(0);
}

/*
* @implemented
*/
VOID
NTAPI
FsRtlReleaseFile(IN PFILE_OBJECT FileObject)
{
    KEBUGCHECK(0);
}

/*++
 * @name FsRtlRegisterFileSystemFilterCallbacks
 * @unimplemented
 *
 * FILLME
 *
 * @param FilterDriverObject
 *        FILLME
 *
 * @param Callbacks
 *        FILLME
 *
 * @return None
 *
 * @remarks None
 *
 *--*/
NTSTATUS
NTAPI
FsRtlRegisterFileSystemFilterCallbacks(IN PDRIVER_OBJECT FilterDriverObject,
                                       IN PFS_FILTER_CALLBACKS Callbacks)
{
    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

