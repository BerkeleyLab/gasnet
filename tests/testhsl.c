/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testhsl.c,v $
 *     $Date: 2006/02/20 19:48:45 $
 * $Revision: 1.11.4.1 $
 * Description: GASNet barrier performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#include <test.h>

int flag = 0;
void okhandler1(gasnet_token_t token) {
  gasnet_hold_interrupts();
  flag++;
}
void okhandler2(gasnet_token_t token) {
  gasnet_resume_interrupts();
  flag++;
}

gasnet_hsl_t globallock = GASNET_HSL_INITIALIZER;
void badhandler1(gasnet_token_t token) {
  gasnet_hsl_lock(&globallock);
}
void badhandler2(gasnet_token_t token) {
  gasnet_hsl_lock(&globallock);
  gasnet_AMReplyShort0(token, 255);
}

void donothing(gasnet_token_t token) {
}

#ifdef __SUNPRO_C
  /* disable a harmless warning */
  #pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif

int main(int argc, char **argv) {
  int mynode, nodes, partner;
  gasnet_handlerentry_t htable[] = { 
    { 201, okhandler1 },
    { 202, okhandler2 },

    { 231, badhandler1 },
    { 232, badhandler2 },

    { 255, donothing }
  };

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t), 
                            TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  test_init("testhsl",0);

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  partner = (gasnet_mynode() + 1) % gasnet_nodes();

  if (argc < 2) {
    printf("Usage: %s (errtestnum:1..16)\n", argv[0]);fflush(stdout);
    gasnet_exit(1);
  }
  {
    int errtest = atoi(argv[1]);
    gasnet_hsl_t lock1 = GASNET_HSL_INITIALIZER;
    gasnet_hsl_t lock2;
    gasnet_hsl_init(&lock2);

    MSG("testing legal cases...");
    gasnet_hsl_lock(&lock1);
    gasnet_resume_interrupts(); /* ignored */
    gasnet_hsl_unlock(&lock1);

    gasnet_hsl_lock(&lock1);
    gasnet_hold_interrupts(); /* ignored */
    gasnet_hsl_unlock(&lock1);

    gasnet_hsl_lock(&lock1);
    gasnet_hold_interrupts(); /* ignored */
    gasnet_resume_interrupts(); 
    gasnet_hsl_unlock(&lock1);

    gasnet_hsl_lock(&lock1);
    assert(mynode == gasnet_mynode()); 
    assert(nodes == gasnet_nodes());
    gasnet_hsl_unlock(&lock1);

    gasnet_hold_interrupts();
    assert(mynode == gasnet_mynode()); 
    assert(nodes == gasnet_nodes());
    gasnet_resume_interrupts(); 

    gasnet_AMRequestShort0(gasnet_mynode(), 201);
    GASNET_BLOCKUNTIL(flag == 1);

    gasnet_AMRequestShort0(gasnet_mynode(), 202);
    GASNET_BLOCKUNTIL(flag == 2);

    MSG("testing illegal case %i...", errtest);
    switch(errtest) {
      case 1:
        gasnet_hold_interrupts();
        gasnet_hold_interrupts();
      break;
      case 2:
        gasnet_resume_interrupts();
      break;
      case 3:
        gasnet_hsl_init(&lock1);
      break;
      case 4:
        gasnet_hsl_destroy(&lock1);
        gasnet_hsl_destroy(&lock1);
      break;
      case 5:
        gasnet_hsl_unlock(&lock1);
      break;
      case 6:
        gasnet_hsl_lock(&lock1);
        gasnet_hsl_lock(&lock2);
        gasnet_hsl_unlock(&lock1);
      break;
      case 7:
        gasnet_hsl_lock(&lock1);
        gasnet_hsl_lock(&lock1);
      break;
      case 8:
        gasnet_hsl_trylock(&lock1);
        gasnet_hsl_trylock(&lock1);
      break;
      case 9:
        gasnet_hsl_lock(&lock1);
        gasnet_AMPoll();
      break;
      case 10:
        gasnet_hold_interrupts();
        gasnet_AMPoll();
      break;
      case 11:
        gasnet_AMRequestShort0(gasnet_mynode(), 231);
        GASNET_BLOCKUNTIL(0);
      break;
      case 12:
        gasnet_AMRequestShort0(gasnet_mynode(), 232);
        GASNET_BLOCKUNTIL(0);
      break;
      case 13:
        gasnet_hsl_lock(&lock1);
        gasnet_AMRequestShort0(gasnet_mynode(), 255);
        gasnet_hsl_unlock(&lock1);
      break;
      case 14:
        gasnet_hsl_lock(&lock1);
        sleep(2);
        gasnet_hsl_unlock(&lock1);
        goto done;
      break;
      case 15:
        gasnet_hsl_trylock(&lock1);
        sleep(2);
        gasnet_hsl_unlock(&lock1);
        goto done;
      break;
      case 16:
        gasnet_hold_interrupts();
        sleep(2);
        gasnet_resume_interrupts();
        goto done;
      break;
      default:
        MSG("bad err test num.");
        abort();
    }
  }

  MSG("ERROR: FAILED: err test failed.");
  abort();

done:
  BARRIER();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}
