/**********************************************************
  *
  *	 stdcc.c - additions to the Kerberos 5 library to support the memory credentical cache API
  *	
  *	  Revision 1.1.1.1 - Frank Dabek July 1998
  *
  **********************************************************/

#include "stdcc.h"
#include "stdcc_util.h"
#include "string.h"
#include <stdio.h>

#if defined(_MSDOS) || defined(_WIN32)
apiCB *gCntrlBlock = NULL;
#endif

//declare our global object wanna-be
//must be installed in ccdefops.c
krb5_cc_ops krb5_cc_stdcc_ops = {
     0,
     "API",
      krb5_stdcc_get_name,
      krb5_stdcc_resolve,
      krb5_stdcc_generate_new,
      krb5_stdcc_initialize,
      krb5_stdcc_destroy,
      krb5_stdcc_close,
      krb5_stdcc_store,
      krb5_stdcc_retrieve,
      krb5_stdcc_get_principal,
      krb5_stdcc_start_seq_get,
      krb5_stdcc_next_cred,
      krb5_stdcc_end_seq_get,
      krb5_stdcc_remove, 
      krb5_stdcc_set_flags,
};

// -- generate_new  --------------------------------
// - create a new cache with a unique name, corresponds to creating a named cache
// - iniitialize the API here if we have to.
krb5_error_code KRB5_CALLCONV  krb5_stdcc_generate_new 
       (krb5_context context, krb5_ccache *id ) 
	
  {
  
  	krb5_ccache newCache;
	char name[kStringLiteralLen];
	cc_time_t time;
	int err;
	
  	//make sure the API has been intialized
  	if (gCntrlBlock == NULL) {
  	  err = cc_initialize(&gCntrlBlock, CC_API_VER_1, NULL, NULL);
  	  if (err != CC_NOERROR) return err;
    }
  	
  	//allocate the cache structure
  	newCache = (krb5_ccache) malloc(sizeof(struct _krb5_ccache));
  	if (newCache == NULL) return KRB5_CC_NOMEM;
  	
  	//create a unique name
  	cc_get_change_time(gCntrlBlock, &time);
  	sprintf(name, "gen_new_cache%d", time);
  	
  	//create the new cache
  	err = cc_create(gCntrlBlock, name, name, CC_CRED_V5,  
  			0L, &(((stdccCacheDataPtr)(newCache->data))->NamedCache) );
	if (err != CC_NOERROR) return err;
	
  	//setup some fields
  	newCache->ops = &krb5_cc_stdcc_ops;
  	newCache->data = (stdccCacheDataPtr)malloc(sizeof(stdccCacheData));
  	
  	//return a pointer to the new cache
	*id = newCache;
	  	
	 return CC_NOERROR;
  }
  
// -- resolve ------------------------------
//
// - create a new cache with the name stored in residual
krb5_error_code KRB5_CALLCONV  krb5_stdcc_resolve 
        (krb5_context context, krb5_ccache *id , const char *residual ) {

	krb5_ccache newCache;
	int err;
	char *cName;
	
  	//make sure the API has been intialized
  	if (gCntrlBlock == NULL) {
  	  err = cc_initialize(&gCntrlBlock, CC_API_VER_1, NULL, NULL);
  	  if (err != CC_NOERROR) return err;
    }
    
    newCache = (krb5_ccache) malloc(sizeof(struct _krb5_ccache));
  	if (newCache == NULL) return KRB5_CC_NOMEM;
  	
  	newCache->ops = &krb5_cc_stdcc_ops;
  	newCache->data = (stdccCacheDataPtr)malloc(sizeof(stdccCacheData));
  	if (newCache->data == NULL) return KRB5_CC_NOMEM;
  	
 	cName = (char *)residual;
	//attempt to find a cache by the same name before creating it
 	err = cc_open(gCntrlBlock, cName, CC_CRED_V5, 0L, &(((stdccCacheDataPtr)(newCache->data))->NamedCache));
 	//we didn't find it.. create it.
 	if (err) {
		err = cc_create(gCntrlBlock, cName, cName, CC_CRED_V5, 
			0L, &(((stdccCacheDataPtr)(newCache->data))->NamedCache) );
		if (err != CC_NOERROR) return err; //still an error, return it
	}
	
  	//return new cache structure
  	*id = newCache;
  	return CC_NOERROR;
  }
  
 // -- initialize --------------------------------
 //-initialize the cache, check to see if one already exists for this principal
 //	if not set our principal to this principal. This searching enables ticket sharing
krb5_error_code KRB5_CALLCONV  krb5_stdcc_initialize 
       (krb5_context context, krb5_ccache id,  krb5_principal princ) 

  {
  
  	int err, err1, found;
  	char *cName = NULL;
  	ccache_p *testNC = NULL;
  	ccache_cit *it;
  	char *p = NULL, *targetName = NULL;
  	
  	//test id for null
  	if (id == NULL) return KRB5_CC_NOMEM;
  	
  	//test for initialized API
  	if (gCntrlBlock == NULL)
  		return CC_NO_EXIST; 


#if defined(_MSDOS) || defined(_WIN32)

	cName = calloc(1, strlen(krb5_princ_name(context, princ)->data) + 1);	
	sprintf(cName, "%s", krb5_princ_name(context, princ)->data);

#else

	//create a principal name for the named cache
	err = krb5_unparse_name(context, princ, &cName);
	if (err)
		return(err);
#endif

		
	//look for a cache already extant for this principal
	it = NULL;
	found = err = 0;
	while ((err != CC_END) && (!found)) {
		err = cc_seq_fetch_NCs(gCntrlBlock, &testNC, &it);
		if (err == CC_NOERROR) {
			cc_get_principal(gCntrlBlock, testNC, &p);
			if (strcmp(p, cName) == 0) {
				found = 1;
				cc_get_name(gCntrlBlock, testNC, &targetName);
			}
			cc_free_principal(gCntrlBlock, &p);
			err1 = cc_close(gCntrlBlock, &testNC);
		}
	}
	
	if (!found)  
		//we didn't find one with the name we were looking for, use the one we had and change the name
		cc_set_principal(gCntrlBlock, (((stdccCacheDataPtr)(id->data))->NamedCache), CC_CRED_V5, cName);
	else {
#if defined(macintosh)
 		//use the default cache
		err = cc_open(gCntrlBlock, targetName, CC_CRED_V5, 0L, &(((stdccCacheDataPtr)(id->data))->NamedCache));
#else
 		//we found a cache for this guy, lets trash ours and use that one
		cc_destroy(gCntrlBlock, &(((stdccCacheDataPtr)(id->data))->NamedCache));
		cc_create(gCntrlBlock, targetName, krb5_princ_name(context, princ)->data, CC_CRED_V5, 0L, &(((stdccCacheDataPtr)(id->data))->NamedCache));
#endif
		if (err != CC_NOERROR) return err; //error opening
		cc_free_name(gCntrlBlock, &targetName);
	}
	
	free(cName);
	
	return CC_NOERROR;
	
	}


// -- store ----------------------------------
// - store some credentials in our cache
krb5_error_code KRB5_CALLCONV krb5_stdcc_store 
        (krb5_context context, krb5_ccache id , krb5_creds *creds )  {
       
       cred_union *cu = NULL;
       int err;
       
  		//test for initialized API
  		if (gCntrlBlock == NULL)
  			return CC_NO_EXIST; 

		//copy the fields from the almost identical structures
		dupK5toCC(context, creds, &cu);
			
		//finally store the credential
		//store will copy (that is duplicate) everything
		err = cc_store(gCntrlBlock, ((stdccCacheDataPtr)(id->data))->NamedCache, *cu);
		if (err != CC_NOERROR) return err;
		
		//free the cred union
		err = cc_free_creds(gCntrlBlock, &cu);
		 
		return err;
}


// -- start_seq_get --------------------------
// - begin an iterator call to get all of the credentials in the cache
krb5_error_code KRB5_CALLCONV krb5_stdcc_start_seq_get 
(krb5_context context, krb5_ccache id , krb5_cc_cursor *cursor ) {

	//all we have to do is initialize the cursor
	*cursor = NULL;
	return CC_NOERROR;
}

// -- next cred ---------------------------
// - get the next credential in the cache as part of an iterator call
// - this maps to call to cc_seq_fetch_creds
krb5_error_code KRB5_CALLCONV krb5_stdcc_next_cred 
        (krb5_context context, 
		   krb5_ccache id , 
		   krb5_cc_cursor *cursor , 
		   krb5_creds *creds ) {

	int err;
	cred_union *credU = NULL;
	
	//test for initialized API
	if (gCntrlBlock == NULL)
		return CC_NO_EXIST; 

	err = cc_seq_fetch_creds(gCntrlBlock, ((stdccCacheDataPtr)(id->data))->NamedCache,
							 &credU, (ccache_cit **)cursor);
	
	if (err != CC_NOERROR)
		return err;

	
	//copy data	(with translation)
	dupCCtoK5(context, credU->cred.pV5Cred, creds);
	
	//free our version of the cred
	cc_free_creds(gCntrlBlock, &credU);
	
	return CC_NOERROR;
}


// -- retreive -------------------
// - try to find a matching credential in the cache
krb5_error_code KRB5_CALLCONV krb5_stdcc_retrieve 
       		(krb5_context context, 
		   krb5_ccache id, 
		   krb5_flags whichfields, 
		   krb5_creds *mcreds, 
		   krb5_creds *creds ) {
		   
	krb5_cc_cursor curs = NULL;
	krb5_creds *fetchcreds;
		
	fetchcreds = (krb5_creds *)malloc(sizeof(krb5_creds));
  	if (fetchcreds == NULL) return KRB5_CC_NOMEM;
	
	//we're going to use the iterators
	krb5_stdcc_start_seq_get(context, id, &curs);
	
	while  (krb5_stdcc_next_cred(context, id, &curs, fetchcreds) == CC_NOERROR) {
		//look at each credential for a match
		//use this match routine since it takes the whichfields and the API doesn't
		if (stdccCredsMatch(context, fetchcreds, mcreds, whichfields)) {
				//we found it, copy and exit
				*creds = *fetchcreds;
				krb5_stdcc_end_seq_get(context, id, &curs);
				return CC_NOERROR;
			}
		//free copy allocated by next_cred
		krb5_free_cred_contents(context, fetchcreds);
		}
		
	//no luck, end get and exit
	krb5_stdcc_end_seq_get(context, id, &curs);
	
	//we're not using this anymore so we should get rid of it!
	free(fetchcreds);
	
	return KRB5_CC_NOTFOUND;
}

// -- end seq ------------------------
// - just free up the storage assoicated with the cursor (if we could)
krb5_error_code KRB5_CALLCONV krb5_stdcc_end_seq_get 
        (krb5_context context, krb5_ccache id , krb5_cc_cursor *cursor ) {
        
       //the limitation of the Ccache api and the seq calls
       //causes trouble. cursor might have already been freed
       //and anyways it is in the mac's heap so we need FreePtr
       //but all i have is free
      // FreePtr(*cursor);
      
      //LEAK IT!
       *cursor = NULL;

	   return(0);
     }
     
// -- close ---------------------------
// - free our pointers to the NC
krb5_error_code KRB5_CALLCONV 
krb5_stdcc_close(krb5_context context, krb5_ccache id)
{
	//free it
	
	if (id->data != NULL)
	
		{
		free((stdccCacheDataPtr)(id->data));
		//null it out
		(stdccCacheDataPtr)(id->data) = NULL;
		}
	
	//I suppose we ought to check if id is null before doing this, but no other place in krb5 does... -smcguire
	free(id);
	
	id = NULL;
	
	return CC_NOERROR;
}

// -- destroy -------------
// - free our storage and the cache
krb5_error_code KRB5_CALLCONV
krb5_stdcc_destroy (krb5_context context, krb5_ccache id ) {

	int err;
	
	//test for initialized API
	if (gCntrlBlock == NULL)
		return CC_NO_EXIST; 

	//destroy the named cache
	err = cc_destroy(gCntrlBlock, &(((stdccCacheDataPtr)(id->data))->NamedCache));
	//free the pointer to the record that held the pointer to the cache
	cc_shutdown(&gCntrlBlock);

	free((stdccCacheDataPtr)(id->data));
	//null it out
	(stdccCacheDataPtr)(id->data) = NULL;
	
	return err;
}

	
// -- getname ---------------------------
// - return the name of the named cache
char * KRB5_CALLCONV krb5_stdcc_get_name 
        (krb5_context context, krb5_ccache id ) {
        
	char *ret = NULL;
	int err;
	
	//test for initialized API
	if (gCntrlBlock == NULL)
		return NULL;
	
	//just a wrapper
	err = cc_get_name(gCntrlBlock, (((stdccCacheDataPtr)(id->data))->NamedCache), &ret);
	
	if (err != CC_NOERROR)
		return ret;
	else
		return NULL;
       		
}

// -- get_principal ---------------------------
// - return the principal associated with the named cache
krb5_error_code KRB5_CALLCONV
krb5_stdcc_get_principal (krb5_context context, krb5_ccache id , krb5_principal *princ ) {

	int err;
	char *name = NULL;
	
	//test for initialized API
	if (gCntrlBlock == NULL)
		return NULL;

	//another wrapper
	err = cc_get_principal(gCntrlBlock, (((stdccCacheDataPtr)(id->data))->NamedCache), &name);

	if (err != CC_NOERROR) 
		return err;
		
	//turn it into a krb principal
	err = krb5_parse_name(context, name, princ);
	
#if defined(macintosh)
	/* have to do something special on the Mac because name has been allocated with
	   Mac memory routine NewPtr and held in memory */
	if (name != NULL)
	{
		UnholdMemory(name,GetPtrSize(name));
		DisposePtr(name);
	}
#else
	if (name != NULL)
		free(name);
#endif

	return err;	
}

// -- set_flags ---------------------------
// - currently a NOP since we don't store any flags in the NC
krb5_error_code KRB5_CALLCONV krb5_stdcc_set_flags 
        (krb5_context context, krb5_ccache id , krb5_flags flags ) {

	return CC_NOERROR;
}

// - remove ---------------------------
// - remove the specified credentials from the NC
krb5_error_code KRB5_CALLCONV krb5_stdcc_remove 
        (krb5_context context, krb5_ccache id , krb5_flags flags, krb5_creds *creds ) {
    
    	cred_union *cu = NULL;
    	int err;
    	
		//test for initialized API
		if (gCntrlBlock == NULL)
			return CC_NO_EXIST; 

    	//convert to a cred union
    	dupK5toCC(context, creds, &cu);
    	
    	//remove it
    	err = cc_remove_cred(gCntrlBlock, (((stdccCacheDataPtr)(id->data))->NamedCache), *cu);
    	if (err != CC_NOERROR) return err;
    	
    	//free the temp cred union
    	err = cc_free_creds(gCntrlBlock, &cu);
    	if (err != CC_NOERROR) return err;

        return CC_NOERROR;
      }
     
