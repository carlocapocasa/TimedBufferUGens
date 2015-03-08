#include "SC_PlugIn.h"

static InterfaceTable *ft;

// For RecordST
// from server/plugins/DelayUGens.cpp
// keep in sync manually
#define TAKEDOWN_IN \
  if(unit->mIn){ \
    RTFree(unit->mWorld, unit->mIn); \
  }

#define CHECK_BUF \
  if (!bufData) { \
                unit->mDone = true; \
    ClearUnitOutputs(unit, inNumSamples); \
    return; \
  }

// Modified SETUP_IN from DelayUGens.cpp
// Since once channel of the buffer is used to keep track
// of times, it requires one more channel in the buffer
// than the number of inputs
// These needs to be manually kept in sync for new SC versions
// and the modification added

#define SETUP_IN_ST(offset) \
  uint32 numInputs = unit->mNumInputs - (uint32)offset; \
  if ((numInputs + 1) != bufChannels) { \
    if(unit->mWorld->mVerbosity > -1 && !unit->mDone){ \
      Print("RecordST channel mismatch: numInputs %i, yet buffer has %i channels. buffer needs one more channel than numInputs to store time\n", numInputs, bufChannels); \
    } \
    unit->mDone = true; \
    ClearUnitOutputs(unit, inNumSamples); \
    return; \
  } \
  if(!unit->mIn){ \
    unit->mIn = (float**)RTAlloc(unit->mWorld, numInputs * sizeof(float*)); \
    if (unit->mIn == NULL) { \
      unit->mDone = true; \
      ClearUnitOutputs(unit, inNumSamples); \
      return; \
    } \
  } \
  float **in = unit->mIn; \
  for (uint32 i=0; i<numInputs; ++i) { \
    in[i] = ZIN(i+offset); \
  }
// end server/plugins/DelayUGens.cpp

// Direct copy from DelayUGens.cpp, update manually
// Potentially use for looping

/*
inline double sc_loop(Unit *unit, double in, double hi, int loop)
{
  // avoid the divide if possible
  if (in >= hi) {
    if (!loop) {
      unit->mDone = true;
      return hi;
    }
    in -= hi;
    if (in < hi) return in;
  } else if (in < 0.) {
    if (!loop) {
      unit->mDone = true;
      return 0.;
    }
    in += hi;
    if (in >= 0.) return in;
  } else return in;

  return in - hi * floor(in/hi);
}
*/


// For PlayST
static inline bool checkBuffer(Unit * unit, const float * bufData, uint32 bufChannels,
                 uint32 expectedChannels, int inNumSamples)
{
  if (!bufData)
    goto handle_failure;

  if (expectedChannels > bufChannels) {
    if(unit->mWorld->mVerbosity > -1 && !unit->mDone)
      Print("Buffer UGen channel mismatch: expected %i, yet buffer has %i channels\n",
          expectedChannels, bufChannels);
    goto handle_failure;
  }
  return true;

handle_failure:
  unit->mDone = true;
  ClearUnitOutputs(unit, inNumSamples);
  return false;
}

struct PlayST : public Unit
{
  double m_phase;
  float m_fbufnum;
  uint32 m_index;
  double m_next;
  SndBuf *m_buf;
  float m_prevtrig;
};

struct RecordST : public Unit
{
  int32 m_writepos;
  float **mIn;
  float m_fbufnum;
  SndBuf *m_buf;
  float m_previnval;
  double m_phase;
  double m_lastphase;
};

struct IndexST : public Unit
{
  float m_fbufnum;
  SndBuf *m_buf;
  float m_prevtrig;
  double m_val;
};

static void PlayST_next_k(PlayST *unit, int inNumSamples);
static void PlayST_Ctor(PlayST* unit);

static void RecordST_next_k(RecordST *unit, int inNumSamples);
static void RecordST_Ctor(RecordST *unit);

static void IndexST_next_k(IndexST *unit, int inNumSamples);
static void IndexST_Ctor(IndexST* unit);

void PlayST_Ctor(PlayST* unit)
{
  SETCALC(PlayST_next_k);
  
  GET_BUF_SHARED
  if (!checkBuffer(unit, bufData, bufChannels, unit->mNumOutputs, 1))
    return;

  unit->m_fbufnum = -1e9f;
  unit->m_phase = 0; 
  //unit->m_next = 0;
  unit->m_next = buf->data[0];
  unit->m_index = 0;

//  int bufChannels = buf->channels;
//  const float* bufData = buf->data;

  PlayST_next_k(unit, 1);

//  ClearUnitOutputs(unit, 1);

//  for (int i = 0, j = 1; j != bufChannels; i++, j++) {
//    OUT(0)[i] = bufData[j];
//  }

}

void PlayST_next_k(PlayST *unit, int inNumSamples)
{
  float rate     = ZIN0(1);
  float trig     = ZIN0(2);

  GET_BUF_SHARED
  
  int numOutputs = unit->mNumOutputs;
  if (!checkBuffer(unit, bufData, bufChannels, numOutputs, inNumSamples))
    return;

  double phase = unit->m_phase;
  double next = unit->m_next;
  uint32 index = unit->m_index;

  const float* frame = bufData + index * bufChannels;

  int silentFrame = 0;

  //if (index < bufFrames) printf("ST: index:%i bufnum:%i bufChannels:%i BUFDUR:%f phase:%f next:%f time:%f note:%f value:%f\n", index, (int) unit->m_fbufnum, bufChannels, BUFDUR, phase, next, frame[0], frame[1], frame[2]);

  if (trig > 0.f && unit->m_prevtrig <= 0.f) {

    phase = ZIN0(3);
    
//    printf("ST: buffer dump ");
//    for (int i = 0; i < (bufFrames * bufChannels); i++) {
//      printf("%f ", bufData[i]);
//    }
//    printf("phase:%f next:%f time:%f note:%f value:%f\n", phase, next, frame[0], frame[1], frame[2]);

    if (phase == 0) {
      next = bufData[0];
      index = 0;
    } else if (phase < 0) {
      unit->mDone = true;
      DoneAction(IN0(4), unit);
    } else {
      if (next > phase) {
        //printf("ST: trackback\n");
        while (next > phase && index > 0) {
          //printf("ST: trackback index:%i next:%f phase:%f\n", index, next, phase);
          index--;
          next -= bufData[index*bufChannels];
        }
      } else {
        // if phase==next, do nothing
        //printf("ST: catchup\n");
        while (next < phase && index < bufFrames) {
          //printf("ST: catchup index:%i next:%f phase:%f\n", index, next, phase);
          next += bufData[index*bufChannels];
          index++;
        }
      }
    }

  } else {

    phase += BUFDUR * rate;

    if (phase >= next) {
      
      if (index < bufFrames) {
        index++;
        next += bufData[index*bufChannels];
        silentFrame = 1;
      } else {
        unit->mDone = true;
        DoneAction(IN0(4), unit);
      }
    }
  }
  
  for (int i = 0, j = 1; j < bufChannels; i++, j++) {
    if (rate > 0 && silentFrame == 0) {
      OUT(i)[0] = frame[j];
    } else {
      OUT(i)[0] = 0;
    }
  }

  unit->m_index = index;
  unit->m_phase = phase;
  unit->m_next = next;
  unit->m_prevtrig = trig;
}


void RecordST_Ctor(RecordST *unit)
{
  
  int inNumSamples = 1;
  unit->mIn = 0;

  GET_BUF
  CHECK_BUF
  SETUP_IN_ST(3)
  
  unit->m_writepos = 0;

  unit->m_phase = 0;
  unit->m_lastphase = 0;

  SETCALC(RecordST_next_k);
  
  float inval = *++(in[0]);
  float* table0 = bufData;
  table0[0] = 0;
  table0[1] = inval;
  for (uint32 i = 1, j = 2; j < bufChannels; ++i, ++j) {
    table0[j] = *++(in[i]);
  }
  printf("RecordST init: wrote values ");
  for (uint32 i = 1; i < bufChannels; i++) {
    printf("%f ", table0[i]);
  }
  printf("at time %f to writepos 0\n", unit->m_phase);
  unit->m_previnval = inval;
}
  
void RecordST_Dtor(RecordST *unit)
{
  TAKEDOWN_IN
}

void RecordST_next_k(RecordST *unit, int inNumSamples)
{

  GET_BUF
  CHECK_BUF
  SETUP_IN_ST(3)

  float run     = ZIN0(1);
  float inval     = *++(in[0]);
  int32 writepos = unit->m_writepos;
  double phase = unit->m_phase;

  float* table0 = bufData + writepos;
  
//  table0[0] = *++(in[0]);


//  if (writepos > bufSamples) {   
//    writepos = 0;
//    printf("RecordST: writepos: %i; run: %f; bufSamples: %i, fbufnum: %f, inval: %f\n", writepos, run, bufSamples, fbufnum, inval);
//  }

  //if (inval > 0.f && unit->m_previnval <= 0.f) {
  if (inval != unit->m_previnval) {
    
    // Write time into last note
    table0[0] = phase - unit->m_lastphase;

    printf("RecordST: wrote time %f to writepos %i. ", table0[0], writepos);

    // Shift to next note and write values, time will be written at next invalger
    writepos += bufChannels;
    if (writepos >= bufSamples) {
      // ... or quit if we're full
      unit->mDone = true;
      DoneAction(IN0(2), unit);
    } else {
      table0 = bufData + writepos;
      table0[1] = inval;
      for (uint32 i = 1, j = 2; j < bufChannels; ++i, ++j) {
        table0[j] = *++(in[i]);
      }

      printf("wrote values ");
      for (uint32 i = 1; i < bufChannels; i++) {
        printf("%f ", table0[i]);
      }
      printf("at time %f to writepos %i\n", phase, writepos);

      unit->m_lastphase = phase; 
    }
  }

  phase += BUFDUR;

  unit->m_writepos = writepos;
  unit->m_previnval = inval;
  unit->m_phase = phase;
}

void IndexST_Ctor(IndexST* unit)
{
  SETCALC(IndexST_next_k);
  
  GET_BUF_SHARED
  if (!checkBuffer(unit, bufData, bufChannels, unit->mNumOutputs, 1))
    return;

  unit->m_fbufnum = -1e9f;
  unit->m_val = 0;

  IndexST_next_k(unit, 1);
}

void IndexST_next_k(IndexST *unit, int inNumSamples)
{
  float trig     = ZIN0(1);
  float startPos     = ZIN0(2);

  GET_BUF_SHARED
  
  int numOutputs = unit->mNumOutputs;
  if (!checkBuffer(unit, bufData, bufChannels, numOutputs, inNumSamples))
    return;

  double val = unit->m_val;

  double preval = val;

  if (trig > 0.f && unit->m_prevtrig <= 0.f) {
    val = 0, preval = 0;
    for (uint32 index = 0; index < bufFrames; index ++) {
      val += bufData[index*bufChannels];
      printf("IndexST: val:%f index:%i bufFrames:%i bufChannels:%i\n", val, index, bufFrames, bufChannels);
      if (val > startPos) {
        printf("IndexST: break at preval %f\n", preval);
        break;
      }
      preval = val;
    }
    val = preval;
  }

  OUT(0)[0] = val;

  unit->m_val = val;
  unit->m_prevtrig = trig;
}


PluginLoad(PlayST)
{
    ft = inTable;
    DefineSimpleUnit(IndexST);
    DefineSimpleUnit(PlayST);
    DefineDtorUnit(RecordST);
}

