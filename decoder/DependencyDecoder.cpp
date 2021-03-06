/*
 * DependencyDecoder.cpp
 *
 *  Created on: Apr 8, 2014
 *      Author: yuanz
 */

#include "DependencyDecoder.h"
#include "../util/Constant.h"
#include "HillClimbingDecoder.h"
#include "ClassifierDecoder.h"
#include "../util/StringUtils.h"
#include "../util/Logarithm.h"
#include <float.h>

namespace segparser {

DependencyDecoder::DependencyDecoder(Options* options) : seed(options->seed), options(options), updateTimes(0) {
}

DependencyDecoder::~DependencyDecoder() {
}

DependencyDecoder* DependencyDecoder::createDependencyDecoder(Options* options, int mode, int thread, bool isTrain) {
	if (DecodingMode::HillClimb == mode) {
		// hill climb
		if (!isTrain)
			return new HillClimbingDecoder(options, thread, options->testConvergeIter);
		else
			return new HillClimbingDecoder(options, thread, options->trainConvergeIter);
	}
	else if (DecodingMode::Exact == options->learningMode) {
		// classifier
		return new ClassifierDecoder(options);
	}
	else {
		ThrowException("unrecognized learning method");
	}
	return NULL;
}

void DependencyDecoder::initInst(DependencyInstance* inst, FeatureExtractor* fe) {
	// choose optimal seg/pos and connect to the root
	for (int i = 0; i < inst->numWord; ++i) {
		for (unsigned int j = 0; j < inst->word[i].candSeg.size(); ++j) {
			SegInstance& segInst = inst->word[i].candSeg[j];
			for (int k = 0 ; k < segInst.size(); ++k) {
				segInst.element[k].dep.setIndex(i == 0 ? -1 : 0, 0);
				segInst.element[k].labid = ConstLab::NOTYPE;
			}
		}

		inst->word[i].currSegCandID = 0;
		SegInstance& segInst = inst->word[i].getCurrSeg();
		for (int j = 0; j < segInst.size(); ++j) {
			segInst.element[j].currPosCandID = 0;
		}
	}

	for (int i = 1; i < inst->numWord; ++i) {
		WordInstance& word = inst->word[i];
		SegInstance& segInst = word.getCurrSeg();

		if (fe->pfe) {
			for (int j = 0; j < segInst.size(); ++j) {
				segInst.element[j].dep.setIndex(-1, 0);
				segInst.element[j].labid = ConstLab::NOTYPE;
			}
		}
		else {
			for (int j = 0; j < segInst.size(); ++j) {
				segInst.element[j].dep.setIndex(0, 0);
				segInst.element[j].labid = ConstLab::NOTYPE;
			}
		}
	}

	inst->constructConversionList();
	inst->setOptSegPosCount();

	if (fe->pfe) {
		CacheTable* cache = fe->getCacheTable(inst);
		assert(cache);
		Random r(0);
		int len = inst->getNumSeg();
		vector<bool> toBeSampled(len);
		int id = 1;		// skip root
		for (int i = 1; i < inst->numWord; ++i) {
			SegInstance& segInst = inst->word[i].getCurrSeg();

			for (int j = 0; j < segInst.size(); ++j) {
				if (segInst.element[j].dep.hWord == -1)
					toBeSampled[id] = true;
				else
					toBeSampled[id] = false;
				id++;
			}
		}
		assert(id == len);
		double T = 0.3;
		bool ok = randomWalkSampler(inst, NULL, fe, cache, toBeSampled, r, T);
		while (!ok) {
			T *= 0.5;
			ok = randomWalkSampler(inst, NULL, fe, cache, toBeSampled, r, T);
		}
	}

	inst->buildChild();
}

void DependencyDecoder::removeGoldInfo(DependencyInstance* inst) {
	// choose optimal seg/pos and connect to the root
	for (int i = 0; i < inst->numWord; ++i) {
		for (unsigned int j = 0; j < inst->word[i].candSeg.size(); ++j) {
			SegInstance& segInst = inst->word[i].candSeg[j];
			for (int k = 0 ; k < segInst.size(); ++k) {
				segInst.element[k].dep.setIndex(i == 0 ? -1 : 0, 0);
				segInst.element[k].labid = ConstLab::NOTYPE;
			}
		}

		inst->word[i].currSegCandID = 0;
		SegInstance& segInst = inst->word[i].getCurrSeg();
		for (int j = 0; j < segInst.size(); ++j) {
			segInst.element[j].currPosCandID = 0;
		}
	}

	inst->constructConversionList();
	inst->setOptSegPosCount();

	inst->buildChild();
}

bool DependencyDecoder::isAncestor(DependencyInstance* s, HeadIndex& h, HeadIndex m) {
	// check if h is the ancestor of m
	// copy m
	int cnt = 0;
	while (m.hWord != -1) {
		if (h == m)
			return true;
		m = s->getElement(m).dep;
		cnt++;
		assert(cnt < 10000);
	}
	return false;
}

bool DependencyDecoder::isProj(DependencyInstance* s, HeadIndex& h, HeadIndex& m) {
	HeadIndex& large = m < h ? h : m;
	HeadIndex& small = m < h ? m : h;

	for (int mw = 0; mw < s->numWord; ++mw) {
		SegInstance& segInst = s->word[mw].getCurrSeg();
		for (int ms = 0; ms < segInst.size(); ++ms) {
			HeadIndex m2(mw, ms);

			if (m2 == h || m2 == m)
				continue;

			HeadIndex& h2 = segInst.element[ms].dep;
			if ((m2 < small || large < m2) && (small < h2 && h2 < large))
				return false;
			if ((small < m2 && m2 < large) && (h2 < small || large < h2))
				return false;
		}
	}
	return true;
}

int DependencyDecoder::getBottomUpOrder(DependencyInstance* inst, HeadIndex& arg, vector<HeadIndex>& idx, int id) {
	vector<HeadIndex>& childList = inst->getElement(arg).child;
	for (unsigned int i = 0; i < childList.size(); ++i) {
		idx[id] = childList[i];
		id--;
	}
	for (unsigned int i = 0; i < childList.size(); ++i) {
		id = getBottomUpOrder(inst, childList[i], idx, id);
	}
	return id;
}

void DependencyDecoder::convertScoreToProb(vector<double>& score) {
	double sumScore = -DBL_MAX;
	for (unsigned int i = 0; i < score.size(); ++i) {
		sumScore = logsumexp(sumScore, score[i]);
	}

	for (unsigned int i = 0; i < score.size(); ++i) {
		score[i] = exp(score[i] - sumScore);
	}
}

int DependencyDecoder::samplePoint(vector<double>& prob, Random& r) {
	int len = prob.size();
	for (int i = 1; i < len; ++i)
		prob[i] += prob[i - 1];

	assert(abs(prob[len - 1] - 1.0) < 1e-4);

	double p = r.nextDouble();
	int ret = 0;
	for (; ret < len; ++ret) {
		if (p < prob[ret])
			break;
	}

	// recover the prob list
	for (int i = len - 1; i >= 1; --i) {
		prob[i] -= prob[i - 1];
	}
	return ret;
}

double DependencyDecoder::samplePos1O(DependencyInstance* inst, DependencyInstance* gold, FeatureExtractor* fe, int wordID, Random& r) {
	WordInstance& word = inst->word[wordID];
	vector<double> probList(word.candSeg.size());
	SegInstance& segInst = word.getCurrSeg();

	// sample pos next;
	word.optPosCount = 0;
	double prob = 1.0;
	for (int i = 0; i < segInst.size(); ++i) {
		HeadIndex m(wordID, i);

		SegElement& ele = segInst.element[i];
		probList.resize(ele.candPosNum());

		for (int j = 0; j < ele.candPosNum(); ++j) {
			ele.currPosCandID = j;

			probList[j] = fe->getPos1OScore(inst, m);
			FeatureVector tmpfv;
			fe->pipe->createPosHOFeatureVector(inst, m, true, &tmpfv);
			probList[j] += fe->parameters->getScore(&tmpfv);

			if (gold) {
				SegInstance& goldInst = gold->word[wordID].getCurrSeg();
				double loss = 0.0;

				if (word.currSegCandID == gold->word[wordID].currSegCandID
						&& j != goldInst.element[j].currPosCandID) {
					loss = 1.0;
				}
				probList[j] += loss;
			}

		}

		for (unsigned int z = 0; z < probList.size(); ++z) {
			probList[z] *= 0.5;
		}

		convertScoreToProb(probList);
		int sample = samplePoint(probList, r);
		ele.currPosCandID = sample;
		word.optPosCount += (ele.currPosCandID == 0 ? 1 : 0);
		prob *= probList[ele.currPosCandID];
	}

	for (int j = 0; j < segInst.size(); ++j) {
		segInst.element[j].dep.setIndex(-1, 0);
	}
	return prob;
}

double DependencyDecoder::sampleSeg1O(DependencyInstance* inst, DependencyInstance* gold, FeatureExtractor* fe, int wordID, Random& r) {
	WordInstance& word = inst->word[wordID];
	vector<double> probList(word.candSeg.size());
	int oldSegID = word.currSegCandID;
	// sample seg first
	for (unsigned int i = 0; i < word.candSeg.size(); ++i) {

		word.currSegCandID = i;

		probList[i] = fe->getSegScore(inst, wordID);

		if (gold) {
			SegInstance& goldInst = gold->word[wordID].getCurrSeg();
			double loss = ((int)i != gold->word[wordID].currSegCandID) ?
					1.0 * (word.getCurrSeg().size() + goldInst.size()) : 0.0; // consistent with parameter::wordError
			probList[i] += loss;
		}

	}
	word.currSegCandID = oldSegID;

	for (unsigned int i = 0; i < probList.size(); ++i) {
		probList[i] *= 0.5;
	}

	convertScoreToProb(probList);
	int sample = samplePoint(probList, r);
	updateSeg(inst, word, sample);
	double prob = probList[word.currSegCandID];

	// remove all arcs to this word and build heuristic dep if needed
	for (int i = 0; i < inst->numWord; ++i) {
		if (i == wordID)
			continue;
		SegInstance& tmpSeg = inst->word[i].getCurrSeg();
		for (int j = 0; j < tmpSeg.size(); ++j)
			if (tmpSeg.element[j].dep.hWord == wordID) {
				tmpSeg.element[j].dep.setIndex(-1, 0);
			}
	}

	return prob;
}

void DependencyDecoder::updateSeg(DependencyInstance* inst, WordInstance& word, int newSeg) {
	inst->optSegCount -= word.currSegCandID == 0;
	word.currSegCandID = newSeg;
	inst->optSegCount += word.currSegCandID == 0;
}

void DependencyDecoder::updatePos(WordInstance& word, SegElement& ele, int newPos) {
	word.optPosCount -= ele.currPosCandID == 0;
	ele.currPosCandID = newPos;
	word.optPosCount += ele.currPosCandID == 0;
}

void DependencyDecoder::getFirstOrderVec(DependencyInstance* inst, DependencyInstance* gold,
		FeatureExtractor* fe, HeadIndex& m, CacheTable* cache, bool treeConstraint, vector<HeadIndex>& candH, vector<double>& score) {
	// get cache table
	//CacheTable* cache = fe->getCacheTable(inst);

	int segNum = 0;
	for (int hw = 0; hw < inst->numWord; ++hw) {
		WordInstance& word = inst->word[hw];
		segNum += word.getCurrSeg().size();
	}
	assert(!cache || cache->numSeg == inst->getNumSeg());

	// get pruned list
	vector<bool> isPruned = move(fe->isPruned(inst, m, cache));
	int segID = -1;

	SegElement& predSegEle = inst->getElement(m);
	HeadIndex oldDep = predSegEle.dep;

	for (int hw = 0; hw < inst->numWord; ++hw) {
		WordInstance& word = inst->word[hw];
		SegInstance& segInst = word.getCurrSeg();

		for (int hs = 0; hs < segInst.size(); ++hs) {
			segID++;

			if (hw == m.hWord && hs == m.hSeg) {
				assert(isPruned[segID]);
			}

			if (isPruned[segID]) {
				continue;
			}

			HeadIndex h(hw, hs);

			if (treeConstraint) {
				if (isAncestor(inst, m, h))		// loop
					continue;
			}

			candH.push_back(h);
			predSegEle.dep = h;
			// don't need to update child list
			double arcScore = fe->getArcScore(fe, inst, h, m, cache);
			if (gold) {
				// add loss
				arcScore += fe->parameters->elementError(gold->word[m.hWord], inst->word[m.hWord], m.hSeg);
			}
			score.push_back(arcScore);
		}
	}
	assert(segID == (int)isPruned.size() - 1);
	predSegEle.dep = oldDep;
}

bool DependencyDecoder::randomWalkSampler(DependencyInstance* pred, DependencyInstance* gold, FeatureExtractor* fe,
		CacheTable* cache, vector<bool>& toBeSampled, Random& r, double T) {
	// basically, the subtrees which is not to be sampled will be collapsed into one node
	int len = toBeSampled.size();
	assert(len == pred->getNumSeg());

	vector<bool> inTree(len);
	inTree[0] = true;
	int id = 1;		// skip root
	for (int i = 1; i < pred->numWord; ++i) {
		SegInstance& segInst = pred->word[i].getCurrSeg();

		for (int j = 0; j < segInst.size(); ++j) {
			if (!toBeSampled[id]) {
				assert(id == pred->wordToSeg(i, j));
				assert(segInst.element[j].dep.hWord  != -1);
			}
			else {
				assert(id == pred->wordToSeg(i, j));
				segInst.element[j].dep.setIndex(-1, 0);
			}
			id++;
		}
	}
	assert(id == len);

	for (int i = 1; i < pred->numWord; ++i) {
		SegInstance& segInst = pred->word[i].getCurrSeg();

		for (int j = 0; j < segInst.size(); ++j) {
			HeadIndex curr(i, j);
			int currid = pred->wordToSeg(curr);

			int loop = 0;
			while (!inTree[currid] && loop < 5000) {
				if (toBeSampled[currid]) {
					vector<HeadIndex> candH;
					vector<double> score;

					getFirstOrderVec(pred, gold, fe, curr, cache, false, candH, score);
					assert(score.size() > 0);
					for (unsigned int z = 0; z < score.size(); ++z)
						score[z] *= T;
					convertScoreToProb(score);
					int sample = samplePoint(score, r);

					pred->getElement(curr).dep = candH[sample];
				}
				curr = pred->getElement(curr).dep;
				currid = pred->wordToSeg(curr);

				loop++;
			}
			if (loop >= 5000) {
				//cout << "sample failed T=" << T << endl;
				return false;
			}
			assert(loop < 5000);

			curr.setIndex(i, j);
			currid = pred->wordToSeg(curr);
			loop = 0;
			while (!inTree[currid] && loop < 10000) {
				inTree[currid] = true;
				curr = pred->getElement(curr).dep;
				currid = pred->wordToSeg(curr);
				loop++;
			}
			if (loop >= 10000) {
				cout << "loop bug 2" << endl;
			}
			assert(loop < 10000);
		}
	}
	return true;
}

void DependencyDecoder::cycleErase(DependencyInstance* inst, HeadIndex i, vector<bool>& toBeSampled) {
	int id = inst->wordToSeg(i);
	int cnt = 0;
	while (inst->getElement(i).dep.hWord != -1) {
		if (toBeSampled[id]) {
			HeadIndex next = inst->getElement(i).dep;
			inst->getElement(i).dep.setIndex(-1, 0);
    		i = next;
		}
		else {
			i = inst->getElement(i).dep;
		}
		id = inst->wordToSeg(i);
		cnt++;
		assert(cnt < 10000);
	}
}

void DependencyDecoder::updateSeg(DependencyInstance* pred, DependencyInstance* gold, HeadIndex& m,
		int newSeg, int oldSeg, int baseOptSeg, int baseOptPos, vector<int>& oldPos, vector<HeadIndex>& oldHeadIndex,
		vector<HeadIndex>& relatedChildren, vector<int>& relatedOldParent) {
	WordInstance& word = pred->word[m.hWord];
	pred->optSegCount = baseOptSeg + (newSeg == 0 ? 1 : 0);
	word.currSegCandID = newSeg;
	int index = oldSeg * word.candSeg.size() + newSeg;		// change from oldSeg to i
	word.optPosCount = baseOptPos;

	for (int j = 0; j < word.getCurrSeg().size(); ++j) {
		SegElement& newEle = word.getCurrSeg().element[j];
		int inMapIndex = word.inMap[index][j];
		assert(inMapIndex < (int)oldPos.size());

		bool findOldPos = false;
		// find pos
		for (int k = 0; k < newEle.candPosNum(); ++k)
			if (newEle.candPosid[k] == oldPos[inMapIndex]) {
				findOldPos = true;
				newEle.currPosCandID = k;
				word.optPosCount += (k == 0 ? 1 : 0);
			}
		if (!findOldPos) {
			newEle.currPosCandID = 0;
			word.optPosCount++;
		}

		// find head
		newEle.dep.hWord = -1;
		if (oldHeadIndex[inMapIndex].hWord == m.hWord) {
			// the head is in the changed word, so check if it becomes a self loop
			int oldHeadSegIndex = oldHeadIndex[inMapIndex].hSeg;
			int oldHeadWordIndex = oldHeadIndex[inMapIndex].hWord;
			assert(oldHeadSegIndex < (int)word.outMap[index].size());
			int loop = 0;
			while (loop < 100 && oldHeadWordIndex == m.hWord && word.outMap[index][oldHeadSegIndex] == j) {
				// a self loop, recursively find its head
				assert(oldHeadSegIndex < (int)oldHeadIndex.size());
				oldHeadSegIndex = oldHeadIndex[oldHeadSegIndex].hSeg;
				oldHeadWordIndex = oldHeadIndex[oldHeadSegIndex].hWord;
			}
			assert(loop < 100);		// should not have dead loop
			if (oldHeadWordIndex == m.hWord)
				newEle.dep = HeadIndex(oldHeadWordIndex, word.outMap[index][oldHeadSegIndex]);
			else
				newEle.dep = HeadIndex(oldHeadWordIndex, oldHeadSegIndex);
		}
		else {
			newEle.dep = oldHeadIndex[inMapIndex];
		}

	}

	// change related children
	for (unsigned int j = 0; j < relatedChildren.size(); ++j) {
		assert(relatedOldParent[j] < (int)word.outMap[index].size());
		pred->getElement(relatedChildren[j]).dep = HeadIndex(m.hWord, word.outMap[index][relatedOldParent[j]]);
	}
}


void DependencyDecoder::setGoldSegAndPos(DependencyInstance* pred, DependencyInstance* gold) {
	for (int i = 1; i < pred->numWord; ++i) {
		pred->word[i].currSegCandID = gold->word[i].currSegCandID;
		SegInstance& segInst = pred->word[i].getCurrSeg();
		for (int j = 0; j < segInst.size(); ++j) {
			segInst.element[j].currPosCandID = gold->word[i].getCurrSeg().element[j].currPosCandID;
			segInst.element[j].dep.setIndex(-1, 0);
		}
	}
	pred->constructConversionList();
	pred->setOptSegPosCount();
}

} /* namespace segparser */
