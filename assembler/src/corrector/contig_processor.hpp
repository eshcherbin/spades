/*
 * contig_processor.hpp
 *
 *  Created on: Jun 27, 2014
 *      Author: lab42
 */

#pragma once
#include "sam_reader.hpp"
#include "read.hpp"
#include "include.hpp"



class ContigProcessor {
	string sam_file;
	string contig_file;
	string contig_name;
	string output_contig_file;

	string contig;
	size_t contig_size;
//	cerr << name;
	MappedSamStream sm;
//
//	while (!sm.eof()) {
//	SingleSamRead tmp;
//	sm >>tmp;
	//print tmp.
	bam_header_t *bam_header;
	vector<position_description> charts;
	vector<int> interesting_positions;

public:
	ContigProcessor(string sam_file, string contig_file):sam_file(sam_file), contig_file(contig_file),sm(sam_file){
		bam_header = sm.ReadHeader();
		read_contig();
	}
	void read_contig() {
		io::FileReadStream contig_stream(contig_file);
		io::SingleRead ctg;
		contig_stream >> ctg;
		contig = ctg.sequence().str();
		contig_size = contig.length();
		contig_name = ctg.name();
		INFO("Processing contig of length " << contig.length());
//extention is always "fasta"
		output_contig_file = contig_file.substr(0, contig_file.length() - 5) + "ref.fasta";
		charts.resize(contig.length());
	}

	void UpdateOneRead(SingleSamRead &tmp){
		map<size_t, position_description> all_positions;
		//INFO(tmp.GetName());
		//INFO(tmp.get_contig_id());
		if (tmp.get_contig_id() < 0) {
			return;
		}
		string cur_s = sm.get_contig_name(tmp.get_contig_id());

		if (cur_s != contig_name) {
			WARN("wrong string");
			return;
		}
		tmp.CountPositions(all_positions);

		for (auto iter = all_positions.begin(); iter != all_positions.end(); ++iter) {
			if ((int)iter->first >=0 && iter->first < contig_size)
				charts[iter->first].update(iter->second);
		}
	}
//returns: number of changed nucleotides;
	int UpdateOneBase(size_t i, stringstream &ss){
		char old = (int) toupper(contig[i]);
		size_t maxi = var_to_pos[(int)contig[i]];
		int maxx = charts[i].votes[maxi];
		for (size_t j = 0; j < MAX_VARIANTS; j++) {
			//1.5 because insertion goes _after_ match
			if (maxx < charts[i].votes[j] || (j == INSERTION && maxx * 2 < charts[i].votes[j] * 3)) {
				maxx = charts[i].votes[j];
				maxi = j;
			}
		}
		if (old != pos_to_var[maxi]) {
			INFO("On position " << i << " changing " << old <<" to "<<pos_to_var[maxi]);
			INFO(charts[i].str());
			if (maxi < DELETION) {
				ss <<pos_to_var[maxi];
				return 1;
			} else if (maxi == DELETION) {

				return 1;
			} else if (maxi == INSERTION) {
				string maxj = "";
				//first base before insertion;
				size_t new_maxi = var_to_pos[(int)contig[i]];
				int new_maxx = charts[i].votes[new_maxi];
				for (size_t k = 0; k < MAX_VARIANTS; k++) {
					if (new_maxx < charts[i].votes[k] && (k != INSERTION) && (k != DELETION)) {
						new_maxx = charts[i].votes[k];
						new_maxi = k;
					}
				}
				ss <<pos_to_var[new_maxi];
				int max_ins = 0;
				for (auto iter = charts[i].insertions.begin(); iter != charts[i].insertions.end(); ++iter) {
					if (iter->second > max_ins){
						max_ins = iter->second;
						maxj = iter->first;
					}
				}
				INFO("most popular insertion: " << maxj);
				ss << maxj;
				if (old == maxj[0]) {
					return maxj.length() - 1;
				} else {
					return maxj.length();
				}
			} else {
				//something starnge happened
				WARN("While processing base " << i << " unknown decision was made");
				return -1;
			}
		} else {
			ss << old;
			return 0;
		}
	}
	size_t count_interesting_positions(){
		for( size_t i = 0; i < contig_size; i++) {
			int sum_total = 0;
			for (size_t j = 0; j < MAX_VARIANTS; j++) {
//TODO: remove this condition
				if (j != INSERTION && j != DELETION) {
					sum_total += charts[i].votes[j];
				}
			}
			int variants = 0;
			for (size_t j = 0; j < MAX_VARIANTS; j++) {
//TODO: reconsider this condition
				if (j != INSERTION && j != DELETION && (charts[i].votes[j] > 0.1* sum_total) && (charts[i].votes[j] < 0.9* sum_total) && (sum_total > 20)) {
					variants++;
				}
			}
			if (variants > 1 || contig[i] == UNDEFINED){
				INFO(i)
				INFO(charts[i].str());
				interesting_positions.push_back((int)i);
			}
		}
		return interesting_positions.size();
	}

	void process_sam_file (){
		while (!sm.eof()) {
			SingleSamRead tmp;
			sm >> tmp;
			UpdateOneRead(tmp);
			//	sm >>tmp;
		}
		sm.reset();
		size_t interesting = count_interesting_positions();
		while (!sm.eof()) {
			PairedSamRead tmp;
			sm >>tmp;
		}
		stringstream s_new_contig;
		for (size_t i = 0; i < contig.length(); i ++) {
			DEBUG(charts[i].str());
			UpdateOneBase(i, s_new_contig);
		}
		io::osequencestream oss(output_contig_file);
		oss << io::SingleRead(contig_name, s_new_contig.str());

	}

	//string seq, cigar; pair<size_t, size_t> borders;
};