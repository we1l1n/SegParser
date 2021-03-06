/*
 * Options.cpp
 *
 *  Created on: Mar 27, 2014
 *      Author: yuanz
 */

#include "Options.h"
#include "util/Constant.h"
#include "util/StringUtils.h"
#include <string>
#include <vector>
#include <iostream>

namespace segparser {

using namespace std;

Options::Options() {
	trainFile = "";
	testFile = "";

	outFile = "";
	modelName = "";

	lang = -1;

	train = false;
	test = false;

	trainPruner = true;

	learningMode = DecodingMode::HillClimb;
	testingMode = DecodingMode::HillClimb;

	// parameter
	numIters = 10;
	maxHead = 20;
	pruneThresh = 0.05;

	trainSentences = 1000000;
	testSentences = 1000000;
	maxLength = 100;

	devThread = 5;
	trainThread = 10;

	seed = 0;
	regC = 0.0001;

	// feature;
	useCS = true;			// consecutive sibling
	useGP = true;			// grandparent
	useHO = true;			// high order and global
	useSP = true;			// seg/pos feature

	trainConvergeIter = 200;
	testConvergeIter = 200;

	evalPunc = true;
	useTedEval = false;
	jointSegPos = true;
	earlyStop = 40;

	saveBestModel = true;
	bestScore = -100;
}

Options::~Options() {
}

void Options::processArguments(int argc, char** argv) {
	for(int i = 0; i < argc; ++i) {
		string str(argv[i]);
		vector<string> pair;
		StringSplit(str, ":", &pair);
		if(pair[0].compare("train") == 0) {
			train = true;
		}
		if(pair[0].compare("test") == 0) {
			test = true;
		}
		if(pair[0].compare("iters") == 0) {
			numIters = atoi(pair[1].c_str());
		}
		if(pair[0].compare("output-file") == 0) {
			outFile = pair[1];
		}
		if(pair[0].compare("train-file") == 0) {
			trainFile = pair[1];
		}
		if(pair[0].compare("test-file") == 0) {
			testFile = pair[1];
			if (outFile.empty())
				outFile = testFile + ".res";
		}
		if(pair[0].compare("model-name") == 0) {
			modelName = pair[1];
		}
		if (pair[0].compare("seed") == 0) {
			seed = atoi(pair[1].c_str());
		}
		if (pair[0].compare("devthread") == 0) {
			devThread = atoi(pair[1].c_str());
		}
		if (pair[0].compare("trainthread") == 0) {
			trainThread = atoi(pair[1].c_str());
		}
		if (pair[0].compare("max-sent") == 0) {
			trainSentences = atoi(pair[1].c_str());
		}
		if (pair[0].compare("max-test-sent") == 0) {
			testSentences = atoi(pair[1].c_str());
		}
		if (pair[0].compare("C") == 0) {
			regC = atof(pair[1].c_str());
		}
		if (pair[0].compare("train-converge") == 0) {
			trainConvergeIter = atoi(pair[1].c_str());
		}
		if (pair[0].compare("test-converge") == 0) {
			testConvergeIter = atoi(pair[1].c_str());
		}
		if (pair[0].compare("tedeval") == 0) {
			useTedEval = (pair[1] == "true" ? true : false);
		}
		if (pair[0].compare("joint") == 0) {
			jointSegPos = (pair[1] == "true" ? true : false);
		}
		if (pair[0].compare("evalpunc") == 0) {
			evalPunc = (pair[1] == "true" ? true : false);
		}
		if (pair[0].compare("earlystop") == 0) {
			earlyStop = atoi(pair[1].c_str());
		}
		if (pair[0].compare("savebest") == 0) {
			saveBestModel = (pair[1] == "true" ? true : false);
		}
		if (pair[0].compare("ho") == 0) {
			useHO = (pair[1] == "true" ? true : false);
		}

		//TODO: add useHO option
	}


	string file = trainFile;
	if (file.empty())
		file = testFile;

	lang = findLang(file);
}

int Options::findLang(string file) {
	for (int i = 0; i < PossibleLang::Count; ++i)
		if (file.find(PossibleLang::langString[i]) != string::npos) {
			return i;
		}
	cout << "Warning: unknow language" << endl;
	return PossibleLang::Count;
}

void Options::setPrunerOptions() {
	modelName = modelName + ".pruner";

	test = false;

	trainPruner = false;

	learningMode = DecodingMode::Exact;
	testingMode = DecodingMode::Exact;

	// parameter
	numIters = 10;

	devThread = 1;
	trainThread = 1;

	regC = 0.1;

	// feature;
	useCS = false;			// consecutive sibling
	useGP = false;			// grandparent
	useHO = false;			// high order and global
	useSP = false;

	saveBestModel = false;
}

void Options::outputArg() {
	cout << "------\nFLAGS\n------" << endl;
	cout << "train-file: " << trainFile << endl;
	cout << "test-file: " << testFile << endl;
	cout << "out-file: " << outFile << endl;
	cout << "model-name: " << modelName << endl;
	cout << "train: " << train << endl;
	cout << "test: " << test << endl;
	cout << "training-iterations: " << numIters << endl;
	cout << "seed: " << seed << endl;
	cout << "use consecutive sibling: " << useCS << endl;
	cout << "use grandparent: " << useGP << endl;
	cout << "use grand sibling, tri-sibling and high order: " << useHO << endl;
	cout << "learning mode: " << learningMode << endl;
	cout << "testing mode: " << testingMode << endl;
	cout << "train thread: " << trainThread << endl;
	cout << "dev thread: " << devThread << endl;
	cout << "reg C: " << regC << endl;
	cout << "train converge iter: " << trainConvergeIter << endl;
	cout << "test converge iter: " << testConvergeIter << endl;
	cout << "early stop: " << earlyStop << endl;
	cout << "tedeval: " << useTedEval << endl;
	cout << "joint seg pos: " << jointSegPos << endl;
	cout << "prune: " << trainPruner << endl;
	cout << "save best model: " << saveBestModel << endl;
	cout << "------\n" << endl;
}

} /* namespace segparser */
