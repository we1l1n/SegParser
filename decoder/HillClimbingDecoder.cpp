/*
 * HillClimbingDecoder.cpp
 *
 *  Created on: May 1, 2014
 *      Author: yuanz
 */

#include "HillClimbingDecoder.h"
#include <float.h>
#include "../util/Timer.h"

namespace segparser {

void* hillClimbingThreadFunc(void* instance) {
	HillClimbingDecoder* data = (HillClimbingDecoder*)instance;

	pthread_t selfThread = pthread_self();
	int selfid = -1;
	for (int i = 0; i < data->thread; ++i)
		if (selfThread == data->threadID[i]) {
			selfid = i;
			break;
		}
	assert(selfid != -1);

	data->debug("pending.", selfid);
	bool jobsDone = false;

	while (true) {
		pthread_mutex_lock(&data->taskMutex[selfid]);

		while (data->taskDone[selfid]) {
			 pthread_cond_wait(&data->taskStartCond[selfid], &data->taskMutex[selfid]);
		}

		//if (!data->gold)
		//	data->debug("start working.", selfid);

		if (data->threadExit[selfid]) {
			// jobs done
            data->debug("receive exit signal.", selfid);
			data->taskDone[selfid] = true;
			jobsDone = true;
		}

		pthread_mutex_unlock(&data->taskMutex[selfid]);

		//if (!data->gold)
		//	data->debug("release lock.", selfid);

		if (jobsDone) {
			break;
		}

		DependencyInstance pred = *(data->pred);			// copy the instance
		DependencyInstance* gold = data->gold;
		FeatureExtractor* fe = data->fe;				// shared fe

		double goldScore = -DBL_MAX;
		if (gold) {
			goldScore = fe->parameters->getScore(&gold->fv);
		}

		Random r(data->options->seed + 2 + selfid);		// set different seed for different thread

		int maxIter = 100;

		// begin sampling
		bool done = false;
		int iter = 0;
		double T = 0.25;
		for (iter = 0; iter < maxIter && !done; ++iter) {

			// sample seg/pos
			if (data->sampleSeg) {
				assert(pred.word[0].currSegCandID == 0);
				for (int i = 1; i < pred.numWord; ++i) {
					data->sampleSeg1O(&pred, gold, fe, i, r);
				}
				pred.constructConversionList();
			}

			if (data->samplePos) {
				for (int i = 1; i < pred.numWord; ++i) {
					data->samplePos1O(&pred, gold, fe, i, r);
				}
			}

			CacheTable* cache = fe->getCacheTable(&pred);
			boost::shared_ptr<CacheTable> tmpCache = boost::shared_ptr<CacheTable>(new CacheTable());
			if (!cache) {
				cache = tmpCache.get();		// temporary cache for this run
				tmpCache->initCacheTable(fe->type, &pred, fe->pfe.get(), data->options);
			}

			// sample a new tree from first order
			int len = pred.getNumSeg();
			vector<bool> toBeSampled(len);
			int id = 1;		// skip root
			for (int i = 1; i < pred.numWord; ++i) {
				SegInstance& segInst = pred.word[i].getCurrSeg();

				for (int j = 0; j < segInst.size(); ++j) {
					toBeSampled[id] = true;
					id++;
				}
			}
			assert(id == len);

			Timer ts;
			bool ok = data->randomWalkSampler(&pred, gold, fe, cache, toBeSampled, r, T);
			if (!ok) {
				T *= 0.5;
				continue;
			}
			pred.buildChild();

			int outloop = 0;
            bool outchange = true;
            Timer tc;

            while (outchange && outloop < 20) {
            	outchange = false;
            	outloop++;

    			bool change = true;
    			int loop = 0;
    			while (change && loop < 20) {
    				change = false;
    				loop++;		// avoid dead loop

    				// improve tree
       	      		vector<HeadIndex> idx(len);
       	      		HeadIndex root(0, 0);
       	     		int id = data->getBottomUpOrder(&pred, root, idx, idx.size() - 1);
       	     		assert(id == 0);

       	     		for (unsigned int y = 1; y < idx.size(); ++y) {
       	     			HeadIndex& m = idx[y];

       	     			double depChanged = data->findOptHead(&pred, gold, m, fe, cache);
       	     			assert(depChanged > -1e-6);
       	     			if (depChanged > 1e-6) {
       	   	     			change = true;
       	   	     			outchange = true;
       	     			}

    				}

   	     			// improve bigram

       	      		for (unsigned int i = 0; i < idx.size(); ++i) idx[i] = HeadIndex();
       	     		id = data->getBottomUpOrder(&pred, root, idx, idx.size() - 1);
       	     		assert(id == 0);

       	     		for (unsigned int y = 1; y < idx.size(); ++y) {
       	     			HeadIndex& m = idx[y];

						int mIndex = pred.wordToSeg(m);
						if (mIndex + 1 >= pred.getNumSeg()) {
							continue;
						}

						HeadIndex n = pred.segToWord(mIndex + 1);
						if (pred.getElement(m).dep != pred.getElement(n).dep) {
							// not same head
							continue;
						}

       	     			double depChanged = data->findOptBigramHead(&pred, gold, m, n, fe, cache);
    					assert(depChanged > -1e-6);
       	     			if (depChanged > 1e-6) {
       	   	     			change = true;
       	   	     			outchange = true;
       	     			}
       	     		}

    			}

    			if (loop >= 20) {
    				cout << "Warning: many in loops" << endl;
    			}

                // improve pos
                if (data->samplePos) {
                	vector<HeadIndex> idx(len);
                	HeadIndex root(0, 0);
                	int id = data->getBottomUpOrder(&pred, root, idx, idx.size() - 1);
                	assert(id == 0);

                	for (unsigned int y = 1; y < idx.size(); ++y) {
                		HeadIndex& m = idx[y];
                		double posChanged = data->findOptPos(&pred, gold, m, fe, cache);
                		assert(posChanged > -1e-6);
                		if (posChanged > 1e-6) {
                			// update cache table
                			cache = fe->getCacheTable(&pred);
                			outchange = true;
                		}
                	}
                }

    			if (!cache) {
    				tmpCache = boost::shared_ptr<CacheTable>(new CacheTable());
    				cache = tmpCache.get();		// temporary cache for this run
    				tmpCache->initCacheTable(fe->type, &pred, fe->pfe.get(), data->options);
    			}

            }


            if (outloop >= 20) {
            	cout << "Warning: many out loops" << endl;
            }

			double currScore = fe->getScore(&pred, cache);
			if (gold) {
				for (int i = 1; i < pred.numWord; ++i)
					currScore += fe->parameters->wordDepError(gold->word[i], pred.word[i]);
			}

			pthread_mutex_lock(&data->updateMutex);

			if (data->unChangeIter >= data->convergeIter)
				done = true;
			else if (gold && data->unChangeIter >= data->earlyStopIter && data->bestScore >= goldScore - 1e-6) {
				// early stop
				done = true;
			}

			if (currScore > data->bestScore + 1e-6) {
				data->bestScore = currScore;
				data->best.copyInfoFromInst(&pred);
				if (!done) {
					data->unChangeIter = 0;
				}
			}
			else {
				data->unChangeIter++;
			}

			pthread_mutex_unlock(&data->updateMutex);
		}

		pthread_mutex_lock(&data->taskMutex[selfid]);

		data->taskDone[selfid] = true;

		pthread_cond_signal(&data->taskDoneCond[selfid]);

		pthread_mutex_unlock(&data->taskMutex[selfid]);
	}

    data->debug("exit.", selfid);

    pthread_exit(NULL);
	return NULL;
}

HillClimbingDecoder::HillClimbingDecoder(Options* options, int thread, int convergeIter) : DependencyDecoder(options), bestScore(-DBL_MAX), unChangeIter(0),
		pred(NULL), gold(NULL), fe(NULL), thread(thread), convergeIter(convergeIter), earlyStopIter(options->earlyStop), samplePos(true), sampleSeg(true) {
	// cout << "converge iter: " << convergeIter << endl;
}

HillClimbingDecoder::~HillClimbingDecoder() {
}

void HillClimbingDecoder::initialize() {
	threadID.resize(thread);
	taskMutex.resize(thread);
	taskStartCond.resize(thread);
	taskDoneCond.resize(thread);
	taskDone.resize(thread);
	threadExit.resize(thread);

	//updateMutex = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_init(&updateMutex, NULL);
	pthread_mutex_init(&debugMutex, NULL);
	for (int i = 0; i < thread; ++i) {
		threadID[i] = -1;
		//taskMutex[i] = PTHREAD_MUTEX_INITIALIZER;
		pthread_mutex_init(&taskMutex[i], NULL);
		//taskStartCond[i] = PTHREAD_COND_INITIALIZER;
		pthread_cond_init(&taskStartCond[i], NULL);
		//taskDoneCond[i] = PTHREAD_COND_INITIALIZER;
		pthread_cond_init(&taskDoneCond[i], NULL);
		taskDone[i] = true;
		threadExit[i] = false;

		int rc = pthread_create(&threadID[i], NULL, hillClimbingThreadFunc, (void*)this);

		if (rc) {
			cout << i << " " << rc << endl;
			ThrowException("Create train thread failed.");
		}
	}
}

void HillClimbingDecoder::shutdown() {
	for (int i = 0; i < thread; ++i) {
		// send start signal
		pthread_mutex_lock(&taskMutex[i]);

		assert(taskDone[i]);
		threadExit[i] = true;
		taskDone[i] = false;

		pthread_cond_signal(&taskStartCond[i]);

		pthread_mutex_unlock(&taskMutex[i]);
	}

	//cout << "finish send signal" << endl;

	for (int i = 0; i < thread; ++i) {
		// wait thread
		pthread_join(threadID[i], NULL);
		debug("finish waiting thread", i);
	}

	for (int i = 0; i < thread; ++i) {
		pthread_mutex_destroy(&taskMutex[i]);
		pthread_cond_destroy(&taskStartCond[i]);
		pthread_cond_destroy(&taskDoneCond[i]);
		debug("finish destroy thread", i);
	}

	pthread_mutex_destroy(&updateMutex);
	pthread_mutex_destroy(&debugMutex);
	//cout << "shutdown finish aaa" << endl;
}

void HillClimbingDecoder::debug(string msg, int id) {
	pthread_mutex_lock(&debugMutex);

	//cout << "Thread " << id << ": " << msg << endl;
	cout.flush();

	pthread_mutex_unlock(&debugMutex);
}

void HillClimbingDecoder::startTask(DependencyInstance* pred, DependencyInstance* gold, FeatureExtractor* fe) {
	this->pred = pred;
	this->gold = gold;
	this->fe = fe;

    if (samplePos || sampleSeg)
    	initInst(this->pred, fe);

	bestScore = -DBL_MAX;
	best.copyInfoFromInst(pred);
	unChangeIter = 0;

	for (int i = 0; i < thread; ++i) {
		// send start signal
		pthread_mutex_lock(&taskMutex[i]);

		assert(taskDone[i]);
		taskDone[i] = false;

		pthread_cond_signal(&taskStartCond[i]);

		pthread_mutex_unlock(&taskMutex[i]);
	}
}

void HillClimbingDecoder::waitAndGetResult(DependencyInstance* inst) {
	for (int i = 0; i < thread; ++i) {
		pthread_mutex_lock(&taskMutex[i]);
		while (!taskDone[i]) {
			 pthread_cond_wait(&taskDoneCond[i], &taskMutex[i] );
		}

		pthread_mutex_unlock(&taskMutex[i]);
	}

	best.loadInfoToInst(inst);

	//cout << "hit gold seg: " << hitGoldSegCount << ", hit gold seg and pos: " << hitGoldSegPosCount << ", total runs: " << totRuns << endl;
}

void HillClimbingDecoder::decode(DependencyInstance* inst, DependencyInstance* gold, FeatureExtractor* fe) {
	assert(fe->thread == thread);

	//double goldScore = fe->getScore(gold);

	startTask(inst, NULL, fe);
	waitAndGetResult(inst);
	inst->constructConversionList();
	inst->setOptSegPosCount();
	inst->buildChild();

	//cout << "aaa: score: " << bestScore << "\tgold score:" << goldScore << " " << (bestScore >= goldScore - 1e-6 ? 1 : 0) << endl;

}

void HillClimbingDecoder::train(DependencyInstance* gold, DependencyInstance* pred, FeatureExtractor* fe, int trainintIter) {
	assert(fe->thread == thread);

	startTask(pred, gold, fe);
	//startTask(pred, NULL, fe);
	waitAndGetResult(pred);
	pred->constructConversionList();
	pred->setOptSegPosCount();
	pred->buildChild();

	FeatureVector oldFV;
	fe->getFv(pred, &oldFV);
	double oldScore = fe->parameters->getScore(&oldFV);

	FeatureVector& newFV = gold->fv;
	double newScore = fe->parameters->getScore(&newFV);

	double err = 0.0;
	for (int i = 1; i < pred->numWord; ++i) {
		err += fe->parameters->wordError(gold->word[i], pred->word[i]);
	}

    if (oldScore + err < newScore - 1e-6) {
    	//cout << oldScore + err << " " << newScore << endl;

    	//cout << "use gold seg and pos" << endl;
        setGoldSegAndPos(pred, gold);

        samplePos = false;
        sampleSeg = false;

        startTask(pred, gold, fe);
        waitAndGetResult(pred);
        pred->constructConversionList();
        pred->setOptSegPosCount();
        pred->buildChild();

        oldFV.clear();
        fe->getFv(pred, &oldFV);
        oldScore = fe->parameters->getScore(&oldFV);

    	err = 0.0;
    	for (int i = 1; i < pred->numWord; ++i) {
    		err += fe->parameters->wordError(gold->word[i], pred->word[i]);
    	}

    	//cout << "result: " << oldScore + err << " " << newScore << " " << unChangeIter << endl;

    	samplePos = true;
        sampleSeg = true;
    }

	if (newScore - oldScore < err) {
		FeatureVector diffFV;		// make a copy
		diffFV.concat(&newFV);
		diffFV.concatNeg(&oldFV);
		if (err - (newScore - oldScore) > 1e-4) {
			fe->parameters->update(gold, pred, &diffFV, err - (newScore - oldScore), fe, updateTimes);
		}
	}

    updateTimes++;
}

double HillClimbingDecoder::findOptPos(DependencyInstance* pred, DependencyInstance* gold, HeadIndex& m, FeatureExtractor* fe, CacheTable* cache) {
	//if (options->heuristicDep && m.hSeg != pred->word[m.hWord].getCurrSeg().inNode) {
	//	return false;
	//}

	SegElement & ele = pred->getElement(m);
	if (ele.candPosNum() == 1)
		return 0.0;

	double bestScore = fe->getPartialPosScore(pred, m, cache);

	if (gold) {
		// add loss
		bestScore += fe->parameters->wordError(gold->word[m.hWord], pred->word[m.hWord]);
	}
	double oldScore = bestScore;

	int bestPos = ele.currPosCandID;
	int oldPos = ele.currPosCandID;

	for (int i = 0; i < ele.candPosNum(); ++i) {
		if (i == oldPos)
			continue;

		if (ele.candProb[i] < -15.0)
			continue;

		updatePos(pred->word[m.hWord], ele, i);
		cache = fe->getCacheTable(pred);
		double score = fe->getPartialPosScore(pred, m, cache);
		if (gold) {
			// add loss
			score += fe->parameters->wordError(gold->word[m.hWord], pred->word[m.hWord]);
		}

		if (score > bestScore + 1e-6) {
			bestScore = score;
			bestPos = i;
		}
	}
	updatePos(pred->word[m.hWord], ele, bestPos);
	assert(bestScore - oldScore > 1e-6 || bestPos == oldPos);
	return bestScore - oldScore;
	//return (bestPos != oldPos ? 1.0 : 0.0);
}

double HillClimbingDecoder::findOptHead(DependencyInstance* pred, DependencyInstance* gold, HeadIndex& m, FeatureExtractor* fe, CacheTable* cache) {
	// return score difference

	// get cache table
	assert(!cache || cache->numSeg == pred->getNumSeg());

	// get pruned list
	vector<bool> isPruned = move(fe->isPruned(pred, m, cache));
	int segID = -1;

	SegElement& predSegEle = pred->getElement(m);
	HeadIndex oldDep = predSegEle.dep;
	HeadIndex bestDep = predSegEle.dep;
	double bestScore = fe->getPartialDepScore(pred, m, cache);
	if (gold) {
		// add loss
		bestScore += fe->parameters->wordDepError(gold->word[m.hWord], pred->word[m.hWord]);
	}
	double oldScore = bestScore;

	for (int hw = 0; hw < pred->numWord; ++hw) {
		SegInstance& segInst = pred->word[hw].getCurrSeg();

		for (int hs = 0; hs < segInst.size(); ++hs) {
			segID++;
			if (isPruned[segID]) {
				continue;
			}

			assert(hw != m.hWord || hs != m.hSeg);

			HeadIndex h(hw, hs);
			if (isAncestor(pred, m, h))		// loop
				continue;

			if (h == oldDep)
				continue;

			HeadIndex oldH = predSegEle.dep;
			predSegEle.dep = h;
			pred->updateChildList(h, oldH, m);
			double score = fe->getPartialDepScore(pred, m, cache);
			if (gold) {
				// add loss
				score += fe->parameters->wordDepError(gold->word[m.hWord], pred->word[m.hWord]);
			}

			if (score > bestScore + 1e-6) {
				bestScore = score;
				bestDep = h;
			}
		}
	}
	assert(segID == (int)isPruned.size() - 1);

	HeadIndex oldH = predSegEle.dep;
	predSegEle.dep = bestDep;
	pred->updateChildList(bestDep, oldH, m);

	assert(bestScore - oldScore > 1e-6 || bestDep == oldDep);
	return bestScore - oldScore;
}

double HillClimbingDecoder::findOptBigramHead(DependencyInstance* pred, DependencyInstance* gold, HeadIndex& m, HeadIndex& n, FeatureExtractor* fe, CacheTable* cache) {
	// return score difference

	assert(!cache || cache->numSeg == pred->getNumSeg());

	// get pruned list
	vector<bool> mPruned = move(fe->isPruned(pred, m, cache));
	vector<bool> nPruned = move(fe->isPruned(pred, n, cache));
	assert(mPruned.size() == nPruned.size());

	int segID = -1;

	SegElement& mEle = pred->getElement(m);
	SegElement& nEle = pred->getElement(n);

	HeadIndex oldDep = mEle.dep;
	HeadIndex bestDep = mEle.dep;
	double bestScore = fe->getPartialBigramDepScore(pred, m, n, cache);

	if (gold) {
		// add loss
		if (m.hWord != n.hWord) {
			bestScore += fe->parameters->wordDepError(gold->word[m.hWord], pred->word[m.hWord])
					 + fe->parameters->wordDepError(gold->word[n.hWord], pred->word[n.hWord]);
		}
		else {
			bestScore += fe->parameters->wordDepError(gold->word[m.hWord], pred->word[m.hWord]);
		}
	}
	double oldScore = bestScore;

	for (int hw = 0; hw < pred->numWord; ++hw) {
		SegInstance& segInst = pred->word[hw].getCurrSeg();

		for (int hs = 0; hs < segInst.size(); ++hs) {
			segID++;
			if (mPruned[segID] || nPruned[segID]) {
				continue;
			}

			assert(hw != m.hWord || hs != m.hSeg);

			HeadIndex h(hw, hs);
			if (isAncestor(pred, m, h) || isAncestor(pred, n, h))		// loop
				continue;

			if (h == oldDep)
				continue;

			HeadIndex oldH = mEle.dep;
			mEle.dep = h;
			pred->updateChildList(h, oldH, m);
			nEle.dep = h;
			pred->updateChildList(h, oldH, n);
			double score = fe->getPartialBigramDepScore(pred, m, n, cache);

			if (gold) {
				// add loss
				if (m.hWord != n.hWord) {
					score += fe->parameters->wordDepError(gold->word[m.hWord], pred->word[m.hWord])
							 + fe->parameters->wordDepError(gold->word[n.hWord], pred->word[n.hWord]);
				}
				else {
					score += fe->parameters->wordDepError(gold->word[m.hWord], pred->word[m.hWord]);
				}
			}

			if (score > bestScore + 1e-6) {
				bestScore = score;
				bestDep = h;
			}
		}
	}
	assert(segID == (int)mPruned.size() - 1);

	HeadIndex oldH = mEle.dep;
	mEle.dep = bestDep;
	pred->updateChildList(bestDep, oldH, m);
	nEle.dep = bestDep;
	pred->updateChildList(bestDep, oldH, n);

	assert(bestScore - oldScore > 1e-6 || bestDep == oldDep);
	return bestScore - oldScore;
	//return (bestDep != oldDep ? 1.0 : 0.0);
}

double HillClimbingDecoder::findOptSeg(DependencyInstance* pred, DependencyInstance* gold, HeadIndex& m, FeatureExtractor* fe, CacheTable* cache) {
	WordInstance& word = pred->word[m.hWord];
	if (word.candSeg.size() == 1)
		return 0.0;

	// hard to define the partial score here
	double bestScore = fe->getScore(pred, cache);

	double oldScore = bestScore;

	int bestSeg = word.currSegCandID;
	int oldSeg = word.currSegCandID;

	int baseOptSeg = pred->optSegCount - (oldSeg == 0 ? 1 : 0);
	int baseOptPos = word.optPosCount;
	vector<HeadIndex> oldHeadIndex(word.getCurrSeg().size());
	vector<int> oldPos(word.getCurrSeg().size());
	for (int i = 0; i < word.getCurrSeg().size(); ++i) {
		oldHeadIndex[i] = word.getCurrSeg().element[i].dep;
		oldPos[i] = word.getCurrSeg().element[i].getCurrPos();
		baseOptPos -= (word.getCurrSeg().element[i].currPosCandID == 0 ? 1 : 0);
	}

	vector<HeadIndex> relatedChildren;
	vector<int> relatedOldParent;

	for (int i = 1; i < pred->numWord; ++i) {
		if (i == m.hWord)
			continue;

		for (int j = 0; j < pred->word[j].getCurrSeg().size(); ++j) {
			if (pred->word[j].getCurrSeg().element[j].dep.hWord == m.hWord) {
				relatedChildren.push_back(HeadIndex(i, j));
				relatedOldParent.push_back(pred->word[j].getCurrSeg().element[j].dep.hSeg);
			}
		}
	}

	for (int i = 0; i < (int)word.candSeg.size(); ++i) {
		if (i == oldSeg)
			continue;

		// update segment
		updateSeg(pred, gold, m, i, oldSeg, baseOptSeg, baseOptPos, oldPos, oldHeadIndex, relatedChildren, relatedOldParent);

		cache = fe->getCacheTable(pred);
		double score = fe->getScore(pred, cache);

		if (score > bestScore + 1e-6) {
			bestScore = score;
			bestSeg = i;
		}
	}

	updateSeg(pred, gold, m, bestSeg, oldSeg, baseOptSeg, baseOptPos, oldPos, oldHeadIndex, relatedChildren, relatedOldParent);

	assert(bestScore - oldScore > 1e-6 || bestSeg == oldSeg);
	return bestScore - oldScore;
}

} /* namespace segparser */
