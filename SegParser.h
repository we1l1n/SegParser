/*
 * SegParser.h
 *
 *  Created on: Mar 19, 2014
 *      Author: yuanz
 */

#ifndef SEGPARSER_H_
#define SEGPARSER_H_

#include <boost/multi_array.hpp>
#include <boost/shared_ptr.hpp>
#include <vector>
#include "DependencyPipe.h"
#include "decoder/DevelopmentThread.h"
#include "Parameters.h"
#include "Options.h"
#include "decoder/DependencyDecoder.h"

namespace segparser {

using namespace std;
using namespace boost;

class Parameters;
class DependencyDecoder;
class DevelopmentThread;

class SegParser {
public:
	SegParser(DependencyPipe* pipe, Options* options);
	virtual ~SegParser();
	void train(vector<inst_ptr>& il);
	void trainingIter(vector<inst_ptr>& goldList, vector<inst_ptr>& predList, int iter);
	void checkDevStatus(int iter);

	void outputWeight(ofstream& fout, int type, Parameters* params);
	void outputWeight(string fStr);
	void loadModel(string file);
	void saveModel(string file, Parameters* params);

	void closeDecoder();

	void evaluatePruning();

	DependencyPipe* pipe;
	DependencyDecoder* decoder;
	Parameters* parameters;
	Parameters* devParams;
	DevelopmentThread* dt;
	Options* options;
	SegParser* pruner;

private:
	int devTimes;
};

} /* namespace segparser */
#endif /* SEGPARSER_H_ */
