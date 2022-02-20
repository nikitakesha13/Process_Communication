#include "common.h"
#include <sys/wait.h>
#include "FIFOreqchannel.h"
#include "MQreqchannel.h"
#include "SHMreqchannel.h"
using namespace std;

void single_data_point(RequestChannel* chan, int p, int t, int e){
	datamsg x (p, t, e);
	chan->cwrite (&x, sizeof (datamsg)); // question
	double reply;
	int nbytes = chan->cread (&reply, sizeof(double)); //answer
	cout << "For person " << p <<", at time " << t << ", the value of ecg "<< e <<" is " << reply << endl;
}

void mult_data_points(RequestChannel* chan, int p){
	ofstream ofs("received/x1.csv");
	cout << "Transfering data points" << endl;

	struct timeval start, end;
	gettimeofday(&start, NULL);

	for (double time = 0.0; time < 4.0; time += 0.004){
		datamsg y(p, time, 1);
		chan->cwrite (&y, sizeof (datamsg)); // asking question
		ofs << time << ',';

		double answer_one;
		chan->cread(&answer_one, sizeof(double)); // reading the answer
		ofs << answer_one << ',';

		datamsg z(p, time, 2);
		chan->cwrite (&z, sizeof(datamsg));

		double answer_two;
		chan->cread(&answer_two, sizeof(double));
		ofs << answer_two << endl;
	}

	gettimeofday(&end, NULL);

	double time_taken;
	time_taken = (end.tv_sec - start.tv_sec) * 1e6;
	time_taken = (time_taken + (end.tv_usec - start.tv_usec)) * 1e-6;

	cout << "Completed Successfully, time taken by the program is: " << fixed << time_taken << setprecision(6) << " sec" << endl;

	ofs.close();
}

void request_file(RequestChannel* chan, string filename, int buffercap, int to_alloc, __int64_t rem, char* buf2, char* recvbuf, FILE* fp, filemsg* file){
	struct timeval start, end;
	gettimeofday(&start, NULL);
	while(rem>0){
		file->length = (int) min (rem, (__int64_t) buffercap);
		chan->cwrite(buf2, to_alloc);
		chan->cread(recvbuf, buffercap);
		fwrite(recvbuf, 1, file->length, fp);
		rem -= file->length;
		file->offset += file->length;
	}

	gettimeofday(&end, NULL);

	double time_taken;
	time_taken = (end.tv_sec - start.tv_sec) * 1e6;
	time_taken = (time_taken + (end.tv_usec - start.tv_usec)) * 1e-6;

	cout << "Completed Successfully, time taken to copy a file is: " << fixed << time_taken << setprecision(6) << " sec" << endl;
}

int main(int argc, char *argv[]){
	
	int opt;
	int p;
	double t;
	int e;

	bool time_exists = false;
	bool person_exists = false;
	bool file_exists = false;
	bool new_channel = false;

	int buffercap = MAX_MESSAGE;

	string ipcmethod = "f";
	int nchannels = 1;
	
	string filename = "";
	while ((opt = getopt(argc, argv, "p:t:e:f:c:m:i:")) != -1) {
		switch (opt) {
			case 'p':
				p = atoi (optarg);
				person_exists = true;
				break;
			case 't':
				t = atof (optarg);
				time_exists = true;
				break;
			case 'e':
				e = atoi (optarg);
				break;
			case 'm':
				buffercap = atoi(optarg);
				break;
			case 'f':
				filename = optarg;
				file_exists = true;
				break;
			case 'c':
				new_channel = true;
				nchannels = atoi(optarg);
				break;
			case 'i':
				ipcmethod = optarg;
				break;
		}
	}

	if (fork() == 0){// child process
		char* args[] = {"./server", "-m", (char*) to_string(buffercap).c_str(), "-i", (char*)ipcmethod.c_str(), NULL};
		if (execvp(args[0], args) < 0){
			perror("exec filed");
			exit(0);
		}
	}

	RequestChannel* control_chan = NULL;
	RequestChannel* chan = control_chan;
	vector<RequestChannel*> mult_chan;

	for (int i = 0; i < nchannels; ++i){

		if (ipcmethod == "f"){
			control_chan = new FIFORequestChannel("control", RequestChannel::CLIENT_SIDE);
		}
		else if (ipcmethod == "q"){
			control_chan = new MQRequestChannel("control", RequestChannel::CLIENT_SIDE, buffercap);
		}
		else if (ipcmethod == "m"){
			control_chan = new SHMRequestChannel("control", RequestChannel::CLIENT_SIDE, buffercap);
		}
		chan = control_chan;
		// The second channel is created
		if (new_channel){

			MESSAGE_TYPE ncm = NEWCHANNEL_MSG;
			control_chan->cwrite (&ncm, sizeof (ncm));
			char name [1000];
			control_chan->cread (name, sizeof (name));
			if (ipcmethod == "f"){
				chan = new FIFORequestChannel(name, RequestChannel::CLIENT_SIDE);
			}
			else if (ipcmethod == "q"){
				chan = new MQRequestChannel(name, RequestChannel::CLIENT_SIDE, buffercap);
			}
			else if (ipcmethod == "m"){
				chan = new SHMRequestChannel(name, RequestChannel::CLIENT_SIDE, buffercap);
			}
			cout << "The name of the new channel: " << name << endl;

		}
		else {
			cout << "The original channel is used: " << chan->name() << endl;
		}
		mult_chan.push_back(chan);
	}

	// Requesting a New Channel
	// Need to send a few data points 

	if (time_exists && person_exists){
		for (int i = 0; i < nchannels; ++i){
			single_data_point(mult_chan[i], p, t, e);
		}
	}
	else if (person_exists && !time_exists){
		for (int i = 0; i < nchannels; ++i){
			mult_data_points(mult_chan[i], p);
		}
		// To compare we need to use the cmp -n 1000 received/x1.csv BIMDC/where copied from.csv
	}

	if (file_exists){
		filemsg fm (0,0);	
		int to_alloc = sizeof (filemsg) + filename.size()+1; // to get the length of the file, we add 1 because we need a null byte at the end
		char* buf2  = new char [to_alloc];
		memcpy (buf2, &fm, sizeof (filemsg));
		strcpy (buf2 + sizeof (filemsg), filename.c_str());
		chan->cwrite (buf2, to_alloc);  // I want the file length;
		__int64_t filesize;
		chan->cread (&filesize, sizeof(__int64_t));
		cout << "The total file size is: " << filesize << endl;

		string new_file = "received/" + filename;
		cout << "The file location: " << new_file << endl;
		FILE* fp = fopen(new_file.c_str(), "wb");
		filemsg* file = (filemsg*) buf2;
		__int64_t rem;
		__int64_t last_rem;
		if (filesize % nchannels == 0){
			rem = filesize / nchannels;
			last_rem = rem;
		}
		else {
			last_rem = filesize % nchannels;
			rem = (filesize - last_rem) / nchannels;
			last_rem += rem;
		}
		cout << "The rem value is: " << rem << endl;
		cout << "The last rem is: " << last_rem << endl;
		char* recvbuf = new char [buffercap];
		for (int i = 0; i < nchannels; ++i){
			if (i == nchannels-1){request_file(mult_chan[i], filename, buffercap, to_alloc, last_rem, buf2, recvbuf, fp, file);}
			else {request_file(mult_chan[i], filename, buffercap, to_alloc, rem, buf2, recvbuf, fp, file);}
		}
		fclose(fp);
		delete recvbuf;
		delete buf2;
		
	}

	// closing the channels
	MESSAGE_TYPE m1 = QUIT_MSG;
	for (int i = 0; i < nchannels; ++i){
		mult_chan[i]->cwrite(&m1, sizeof(MESSAGE_TYPE));
		delete mult_chan[i];
	}

	if (chan != control_chan){ // means that a new channel is created
		control_chan->cwrite(&m1, sizeof (MESSAGE_TYPE));
		delete control_chan;
	}
	wait(0);
}
