#include "Aligner.h"
#include "PrefixIterator.h"
#include "Sequence.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <utility>

using namespace std;

namespace opt {
	extern int multimap;
};

/** Convert the specified contig ID string to a numeric index. */
template <class SeqPosHashMap>
unsigned Aligner<SeqPosHashMap>::contigIDToIndex(ContigID id)
{
	pair<ContigDict::const_iterator, bool> inserted
		= m_contigDict.insert(make_pair(id, m_contigDict.size()));
	if (inserted.second)
		m_contigIDs.push_back(id);
	return inserted.first->second;
}

/** Convert the specified contig ID numeric index to a string. */
template <class SeqPosHashMap>
const ContigID& Aligner<SeqPosHashMap>::
contigIndexToID(unsigned index)
{
	return m_contigIDs.at(index);
}

/** Create an index of the target sequence. */
template <class SeqPosHashMap>
void Aligner<SeqPosHashMap>::addReferenceSequence(const ContigID& id, const Sequence& seq)
{
	// Break the ref sequence into kmers of the hash size
	int size = seq.length();
	for(int i = 0; i < (size - m_hashSize + 1); ++i)
	{
		Sequence subseq = seq.substr(i, m_hashSize);
		if (subseq.find("N") != string::npos)
			continue;

		PackedSeq kmer(subseq);
		if (!opt::multimap) {
			class SeqPosHashMap::const_iterator it
				= m_pDatabase->find(kmer);
			if (it != m_pDatabase->end()) {
				cerr << "error: duplicate k-mer in "
					<< it->second.contig << " also in " << id << ": "
					<< kmer.decode() << '\n';
				exit(EXIT_FAILURE);
			}
		}
		Position p;
		p.contig = contigIDToIndex(id);
		p.pos = i;
		m_pDatabase->insert(make_pair(kmer, p));
	}
}

template <class SeqPosHashMap>
template <class oiterator>
void Aligner<SeqPosHashMap>::alignRead(const Sequence& seq,
		oiterator dest)
{
	getAlignmentsInternal(seq, false, dest);
	getAlignmentsInternal(reverseComplement(seq), true, dest);
}

template <class SeqPosHashMap>
template <class oiterator>
void Aligner<SeqPosHashMap>::
getAlignmentsInternal(const Sequence& seq, bool isRC,
		oiterator& dest)
{
	// The results
	AlignmentSet aligns;

	int seqLen = seq.length();
	for(int i = 0; i < (seqLen - m_hashSize) + 1; ++i)
	{
		// Generate kmer
		PackedSeq kmer = seq.substr(i, m_hashSize);

		// Get the alignment positions
		LookupResult result = m_pDatabase->equal_range(kmer);
		
		for (SPHMConstIter resultIter = result.first; resultIter != result.second; ++resultIter)
		{
			//printf("Seq: %s Contig: %s position: %d\n", seq.decode().c_str(), resultIter->second.contig.c_str(), resultIter->second.pos);
			int read_pos;
			
			// The read position coordinate is wrt to the forward read position
			if(!isRC)
			{
				read_pos = i;
			}
			else
			{
				read_pos = Alignment::calculateReverseReadStart(i, seqLen, m_hashSize);
			}

			unsigned ctgIndex = resultIter->second.contig;
			Alignment align(contigIndexToID(ctgIndex),
					resultIter->second.pos, read_pos, m_hashSize,
					seqLen, isRC);
			aligns[ctgIndex].push_back(align);
		}
	}

	coalesceAlignments(aligns, dest);
}

template <class SeqPosHashMap>
template <class oiterator>
void Aligner<SeqPosHashMap>::
coalesceAlignments(const AlignmentSet& alignSet,
		oiterator& dest)
{
	// For each contig that this read hits, coalesce the alignments into contiguous groups
	for(AlignmentSet::const_iterator ctgIter = alignSet.begin(); ctgIter != alignSet.end(); ++ctgIter)
	{
		AlignmentVector alignVec = ctgIter->second;
		
		if(alignVec.empty())
		{
			continue;
		}
		
		// Sort the alignment vector by contig alignment position
		sort(alignVec.begin(), alignVec.end(), compareContigPos);
				
		// Get the starting position
		assert(!ctgIter->second.empty());
		AlignmentVector::iterator prevIter = alignVec.begin();
		AlignmentVector::iterator currIter = alignVec.begin() + 1;
		
		Alignment currAlign = *prevIter;

		while(currIter != alignVec.end())
		{
			//std::cout << "CurrAlign: " << currAlign << "\n";
			//std::cout << "AlignIter: " << *currIter << "\n";
			
			// Discontinuity found
			if(currIter->contig_start_pos != prevIter->contig_start_pos + 1)
			{
				//std::cout << "	Discontinous, saving\n";
				*dest++ = currAlign;
				currAlign = *currIter;
			}
			else
			{
				//bstd::cout << "	Continous, updating\n";
				// alignments are consistent, increase the length of the alignment
				currAlign.align_length++;
				currAlign.read_start_pos = std::min(currAlign.read_start_pos, currIter->read_start_pos);
			}
			
			prevIter = currIter;
			currIter++;
		}

		*dest++ = currAlign;
	}
}

// Explicit instantiation.
template void Aligner<SeqPosHashMultiMap>::addReferenceSequence(
		const ContigID& id, const Sequence& seq);

template void Aligner<SeqPosHashUniqueMap>::addReferenceSequence(
		const ContigID& id, const Sequence& seq);

template void Aligner<SeqPosHashMultiMap>::
alignRead<prefix_ostream_iterator<Alignment> >(
		const Sequence& seq, prefix_ostream_iterator<Alignment> dest);

template void Aligner<SeqPosHashUniqueMap>::
alignRead<prefix_ostream_iterator<Alignment> >(
		const Sequence& seq, prefix_ostream_iterator<Alignment> dest);
