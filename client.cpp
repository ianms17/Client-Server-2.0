/*
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date  : 2/8/19
 */
#include "common.h"
#include <sys/wait.h>
#include <vector>
#include <stdio.h>
#include <unistd.h>
#include "FIFOreqchannel.h"
#include "MQreqchannel.h"
#include "SHMreqchannel.h"

using namespace std;


int main(int argc, char *argv[]){
    
    int c;
    int buffercap = MAX_MESSAGE;
    int p = 0, ecg = 1;
    double t = -1.0;
    bool isnewchan = false;
    bool isfiletransfer = false;
    string filename;
    string ipcmethod = "f";
    int num_channels = 1;
    struct timeval start, end;


    while ((c = getopt (argc, argv, "p:t:e:m:f:c:i:")) != -1){
        switch (c){
            case 'p':
                p = atoi (optarg);
                break;
            case 't':
                t = atof (optarg);
                break;
            case 'e':
                ecg = atoi (optarg);
                break;
            case 'm':
                buffercap = atoi (optarg);
                break;
            case 'c':
                isnewchan = true;
                num_channels = atoi (optarg);
                break;
            case 'f':
                isfiletransfer = true;
                filename = optarg;
                break;
            case 'i':
                ipcmethod = optarg;
                break;
        }
    }
    
    // fork part
    if (fork()==0){ // child 
	
		char* args [] = {"./server", "-m", (char *) to_string(buffercap).c_str(), "-i", (char*) ipcmethod.c_str(), NULL};
        if (execvp (args [0], args) < 0){
            perror ("exec filed");
            exit (0);
        }
    }

    RequestChannel* control_chan = NULL;
    if (ipcmethod == "f") {
        control_chan = new FIFORequestChannel ("control", RequestChannel::CLIENT_SIDE);
    } else if (ipcmethod == "q") {
        control_chan = new MQRequestChannel ("control", RequestChannel::CLIENT_SIDE, buffercap);
    } else if (ipcmethod == "m") {
        control_chan = new SHMRequestChannel ("control", RequestChannel::CLIENT_SIDE, buffercap);
    }

    RequestChannel* chan = control_chan;

    // new_vector stores all c new channels created, c given by -c flag
    vector<RequestChannel*> new_channels;

    /*
    Rewrite logic for new channel flag to support opening multiple channels
    */
    if (isnewchan) {
        
        // num_channels taken as input from optarg
        for (int i = 0; i < num_channels; ++i) {
            // write newmessage to open new channel
            MESSAGE_TYPE m = NEWCHANNEL_MSG;
            control_chan->cwrite (&m, sizeof (m));
            char newchanname [100];
            control_chan->cread (newchanname, sizeof (newchanname));

            if (ipcmethod == "f") {
                chan = new FIFORequestChannel (newchanname, RequestChannel::CLIENT_SIDE);
            } else if (ipcmethod == "q") {
                chan = new MQRequestChannel (newchanname, RequestChannel::CLIENT_SIDE, buffercap);
            } else if (ipcmethod == "m") {
                chan = new SHMRequestChannel (newchanname, RequestChannel::CLIENT_SIDE, buffercap);
            }

            new_channels.push_back(chan);
        }
    } else {
        // even if no new channels created, add the default channel to the vector for uniformity
        new_channels.push_back(chan);
    }

    /*
    RequestChannel* chan = control_chan;
    if (isnewchan){
        cout << "Using the new channel everything following" << endl;
        MESSAGE_TYPE m = NEWCHANNEL_MSG;
        control_chan->cwrite (&m, sizeof (m));
        char newchanname [100];
        control_chan->cread (newchanname, sizeof (newchanname));

        if (ipcmethod == "f") {
            chan = new FIFORequestChannel (newchanname, RequestChannel::CLIENT_SIDE);
        } else if (ipcmethod == "q") {
            chan = new MQRequestChannel (newchanname, RequestChannel::CLIENT_SIDE);
        } else if (ipcmethod == "m") {
            chan = new SHMRequestChannel (newchanname, RequestChannel::CLIENT_SIDE, buffercap);
        }
        
        cout << "New channel by the name " << newchanname << " is created" << endl;
        cout << "All further communication will happen through it instead of the main channel" << endl;
    }
    */
    

    if (!isfiletransfer) {   // requesting data msgs
        // open a file to write 1000 data points per channel into
        ofstream ofs;
        if (t < 0) {
            gettimeofday(&start, NULL);
            ofs.open("received/1000_points.txt");
        }
        
        // iterate through all new channels created, replace chan->cread/write with new_channels[i].cread/write
        for (int i = 0; i < num_channels; ++i) {
            if (t >= 0) {    // 1 data point
                datamsg d (p, t, ecg);
                new_channels[i]->cwrite (&d, sizeof (d));
                double ecgvalue;
                new_channels[i]->cread (&ecgvalue, sizeof (double));
                cout << "Ecg " << ecg << " value for patient "<< p << " at time " << t << " is: " << ecgvalue << endl;
            } else {          // bulk (i.e., 1K) data requests 
                double ts = 0;  
                datamsg d (p, ts, ecg);
                double ecgvalue;
                for (int j=0; j<1000; j++) {
                    new_channels[i]->cwrite (&d, sizeof (d));
                    new_channels[i]->cread (&ecgvalue, sizeof (double));
                    d.seconds += 0.004; //increment the timestamp by 4ms
                    // cout << ecgvalue << endl;
                    ofs << ecgvalue << endl;
                }
                ofs << "-------- END OF CHANNEL REQUEST --------" << endl;
            }
            MESSAGE_TYPE q = QUIT_MSG;
            new_channels[i]->cwrite(&q, sizeof(MESSAGE_TYPE));
        }
        if (t < 0) {
            gettimeofday(&end, NULL);
            ofs.close();
            cout << "Time Taken to Request 1000 Data Points through " << num_channels << " channels: ";
            cout << end.tv_usec - start.tv_usec << " microseconds" << endl;
        }
    }
    else if (isfiletransfer) {
        // part 2 requesting a file
        filemsg f (0,0);  // special first message to get file size
        int to_alloc = sizeof (filemsg) + filename.size() + 1; // extra byte for NULL
        char* buf = new char [to_alloc];
        memcpy (buf, &f, sizeof(filemsg));
        strcpy (buf + sizeof (filemsg), filename.c_str());
        chan->cwrite (buf, to_alloc);
        __int64_t filesize;
        chan->cread (&filesize, sizeof (__int64_t));
        cout << "File size: " << filesize << endl;

        //int transfers = ceil (1.0 * filesize / MAX_MESSAGE);
        filemsg* fm = (filemsg*) buf;
        __int64_t rem = filesize;
        string outfilepath = string("received/") + filename;
        FILE* outfile = fopen (outfilepath.c_str(), "wb");  
        fm->offset = 0;

        char* recv_buffer = new char [MAX_MESSAGE];
        __int64_t rem_channel = rem / num_channels;
        __int64_t rem_file = rem % num_channels;
        for (int i = 0; i < num_channels; ++i) {
            while (rem_channel > 0) {
                fm->length = (int) min (rem_channel, (__int64_t) MAX_MESSAGE);
                new_channels[i]->cwrite (buf, to_alloc);
                new_channels[i]->cread (recv_buffer, MAX_MESSAGE);
                fwrite (recv_buffer, 1, fm->length, outfile);
                rem_channel -= fm->length;
                fm->offset += fm->length;
                //cout << fm->offset << endl;
            }
            if (i > 0) {
                MESSAGE_TYPE q = QUIT_MSG;
                new_channels[i]->cwrite(&q, sizeof(MESSAGE_TYPE));
            }
        }

        if (rem_file > 0) {
            while (rem_file > 0) {
                fm->length = (int) min (rem_file, (__int64_t) MAX_MESSAGE);
                new_channels[0]->cwrite (buf, to_alloc);
                new_channels[0]->cread (recv_buffer, MAX_MESSAGE);
                fwrite (recv_buffer, 1, fm->length, outfile);
                rem_channel -= fm->length;
                fm->offset += fm->length;
            }
        }
        MESSAGE_TYPE q = QUIT_MSG;
        new_channels[0]->cwrite(&q, sizeof(MESSAGE_TYPE));
        fclose (outfile);
        delete recv_buffer;
        delete buf;
        cout << "File transfer completed" << endl;
    }

    // send close message to for every created channel
    MESSAGE_TYPE q = QUIT_MSG;
    /*for (int i = 0; i < num_channels; ++i) {
        new_channels[i]->cwrite (&q, sizeof (MESSAGE_TYPE));
        // delete new_channels[i];
    }*/

    if (new_channels[0] != control_chan) { // this means that the user requested a new channel, so the control_channel must be destroyed as well 
        control_chan->cwrite (&q, sizeof (MESSAGE_TYPE));
    }
	// wait for the child process running server
    // this will allow the server to properly do clean up
    // if wait is not used, the server may sometimes crash
	wait (0);
    
}
