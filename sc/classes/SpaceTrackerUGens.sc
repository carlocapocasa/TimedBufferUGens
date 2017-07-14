PlayBufS : MultiOutUGen {

	*ar { arg numChannels, bufnum=0, rate=1.0, trigger=1.0, startPos=0.0, doneAction=0;
		^this.multiNew('audio', numChannels, bufnum, rate, trigger, startPos, doneAction)
  }

	*kr { arg numChannels, bufnum=0, rate=1.0, trigger=1.0, startPos=0.0, doneAction=0;
		^this.multiNew('control', numChannels, bufnum, rate, trigger, startPos, doneAction)
	}

	init { arg argNumChannels ... theInputs;
		inputs = theInputs;
		^this.initOutputs(argNumChannels, rate);
	}
	argNamesInputsOffset { ^2 }
}

RecordBufS : UGen {
	*ar { arg inputArray, bufnum=0, run=1.0, doneAction=0;
		^this.multiNewList(
			if (inputArray.first.isArray == false) {
        inputArray = [inputArray];
      };
      ['audio', bufnum, run, doneAction ]
			++ inputArray.asArray.multiChannelExpand
		)
	}
	*kr { arg inputArray, bufnum=0, run=1.0, doneAction=0;
		^this.multiNewList(
			if (inputArray.first.isArray == false) {
        inputArray = [inputArray];
      };
			['control', bufnum, run, doneAction ]
			++ inputArray.asArray.multiChannelExpand
		)
	}
}

PlayBufSIndex : UGen {
  *ar {
    thisMethod.notYetImplemented;
  }
	*kr { arg bufnum=0, trigger=1.0, startPos=0.0, controlDurTrunc = 0.0;
		^this.multiNew('control', bufnum, trigger, startPos, controlDurTrunc)
	}
}

