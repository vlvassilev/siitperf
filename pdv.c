/* Siitperf is an RFC 8219 SIIT (stateless NAT64) tester written in C++ using DPDK
 *
 *  Copyright (C) 2019 Gabor Lencse
 *
 *  This file is part of siitperf.
 *
 *  Siitperf is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Siitperf is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with siitperf.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "defines.h"
#include "includes.h"
#include "throughput.h"
#include "pdv.h"

// the understanding of this code requires the knowledge of throughput.c
// only a few functions are redefined or added here

// after reading the parameters for throughput measurement, further one parameter is read
int Pdv::readCmdLine(int argc, const char *argv[]) {
  if ( Throughput::readCmdLine(argc-1,argv) < 0 )
    return -1;
  if ( sscanf(argv[7], "%hu", &frame_timeout) != 1 || frame_timeout >= 1000*duration+global_timeout ) {
    std::cerr << "Input Error: Frame timeout must be less than 1000*duration+global timeout, (0 means PDV measurement)." << std::endl;
    return -1;
  }
  return 0;
}

int Pdv::senderPoolSize(int num_dest_nets) {
  return Throughput::senderPoolSize(num_dest_nets)*N; // all frames exit is N copies
}

// creates a special IPv4 Test Frame for PDV measurement using several helper functions
struct rte_mbuf *mkPdvFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, const uint32_t *dst_ip) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the PDV Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the PDV Frame! \n", side);
  length -=  ETHER_CRC_LEN; // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length; // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *); // Access the Test Frame in the message buffer
  ether_hdr *eth_hdr = reinterpret_cast<struct ether_hdr *>(pkt); // Ethernet header
  ipv4_hdr *ip_hdr = reinterpret_cast<ipv4_hdr *>(pkt+sizeof(ether_hdr)); // IPv4 header
  udp_hdr *udp_hd = reinterpret_cast<udp_hdr *>(pkt+sizeof(ether_hdr)+sizeof(ipv4_hdr)); // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t*>(pkt+sizeof(ether_hdr)+sizeof(ipv4_hdr)+sizeof(udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x0800);       // contains an IPv4 packet
  int ip_length = length - sizeof(ether_hdr);
  mkIpv4Header(ip_hdr, ip_length, src_ip, dst_ip);      // Does not set IPv4 header checksum
  int udp_length = ip_length - sizeof(ipv4_hdr);        // No IP Options are used
  mkUdpHeader(udp_hd, udp_length);
  int data_legth = udp_length - sizeof(udp_hdr);
  mkDataPdv(udp_data, data_legth);
  udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
  return pkt_mbuf;
}

// creates a special IPv6 Test Frame for PDV measurement using several helper functions
struct rte_mbuf *mkPdvFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const struct in6_addr *src_ip, const struct in6_addr *dst_ip) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the PDV Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the PDV Frame! \n", side);
  length -=  ETHER_CRC_LEN; // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length; // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *); // Access the Test Frame in the message buffer
  ether_hdr *eth_hdr = reinterpret_cast<struct ether_hdr *>(pkt); // Ethernet header
  ipv6_hdr *ip_hdr = reinterpret_cast<ipv6_hdr *>(pkt+sizeof(ether_hdr)); // IPv6 header
  udp_hdr *udp_hd = reinterpret_cast<udp_hdr *>(pkt+sizeof(ether_hdr)+sizeof(ipv6_hdr)); // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t*>(pkt+sizeof(ether_hdr)+sizeof(ipv6_hdr)+sizeof(udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ip_length = length - sizeof(ether_hdr);
  mkIpv6Header(ip_hdr, ip_length, src_ip, dst_ip);
  int udp_length = ip_length - sizeof(ipv6_hdr); // No IP Options are used
  mkUdpHeader(udp_hd, udp_length);
  int data_legth = udp_length - sizeof(udp_hdr);
  mkDataPdv(udp_data, data_legth);
  udp_hd->dgram_cksum = rte_ipv6_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  return pkt_mbuf;
}

void mkDataPdv(uint8_t *data, uint16_t length) {
  unsigned i;
  uint8_t identify[8]= { 'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y' };      // Identificion of the Latency Frames
  uint64_t *id=(uint64_t *) identify;
  *(uint64_t *)data = *id;
  data += 8;
  length -= 8;
  *(uint64_t *)data = 0; // place for the 64-bit serial number
  data += 8;
  length -=8;
  for ( i=0; i<length; i++ )
    data[i] = i % 256;
}

// sends Test Frames for PDV measurements
int sendPdv(void *par) {
  // collecting input parameters:
  class senderParametersPdv *p = (class senderParametersPdv *)par;
  class senderCommonParameters *cp = (class senderCommonParameters *) p->cp;
  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  uint16_t duration = cp->duration;
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;

  // parameters which are different for the Left sender and the Right sender
  int ip_version = p->ip_version;
  rte_mempool *pkt_pool = p->pkt_pool;
  uint8_t eth_id = p->eth_id;
  const char *side = p->side;
  struct ether_addr *dst_mac = p->dst_mac;
  struct ether_addr *src_mac = p->src_mac;
  uint16_t num_dest_nets = p->num_dest_nets;
  uint32_t *src_ipv4 = p->src_ipv4;
  uint32_t *dst_ipv4 = p->dst_ipv4;
  struct in6_addr *src_ipv6 = p->src_ipv6;
  struct in6_addr *dst_ipv6 = p->dst_ipv6;
  struct in6_addr *src_bg= p->src_bg;
  struct in6_addr *dst_bg = p->dst_bg;
  uint64_t **send_ts = p->send_ts;

  uint64_t frames_to_send = duration * frame_rate;      // Each active sender sends this number of packets
  uint64_t sent_frames=0; // counts the number of sent frames
  double elapsed_seconds; // for checking the elapsed seconds during sending

  // prepare a NUMA local, cache line aligned array for send timestamps
  uint64_t *snd_ts = (uint64_t *) rte_malloc(0, 8*frames_to_send, 128);
  if ( !snd_ts )
      rte_exit(EXIT_FAILURE, "Error: Receiver can't allocate memory for timestamps!\n");
  *send_ts = snd_ts; // return the address of the array to the caller function

  if ( num_dest_nets== 1 ) {
    // optimized code for single flow: always the same foreground or background frame is sent, but it is updated regarding counter and UDP checksum
    // N size arrays are used to resolve the write after send problem
    int i; // cycle variable for the above mentioned purpose: takes {0..N-1} values
    struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N]; // pointers of message buffers for fg. and bg. PDV Frames
    uint8_t *fg_pkt[N], *bg_pkt[N]; // pointers to the frames (in the message buffers)
    uint8_t *fg_udp_chksum[N], *bg_udp_chksum[N], *fg_counter[N], *bg_counter[N]; 	// pointers to the given fields
    uint16_t fg_udp_chksum_start, bg_udp_chksum_start; 	// starting values (uncomplemented checksums taken from the original frames)
    uint32_t chksum; // temporary variable for shecksum calculation

    // create PDV Test Frames 
    for ( i=0; i<N; i++ ) {
      // create foreground PDV Frame (IPv4 or IPv6)
      if ( ip_version == 4 ) {
        fg_pkt_mbuf[i] = mkPdvFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4);
        fg_pkt[i] = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the PDV Frame in the message buffer
        fg_udp_chksum[i] = fg_pkt[i] + 40;
        fg_counter[i] = fg_pkt[i] + 50;
      } else { // IPv6
        fg_pkt_mbuf[i] = mkPdvFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6);
        fg_pkt[i] = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the PDV Frame in the message buffer
        fg_udp_chksum[i] = fg_pkt[i] + 60;
        fg_counter[i] = fg_pkt[i] + 70;
      }
      fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
      // create backround PDV Frame (always IPv6)
      bg_pkt_mbuf[i] = mkPdvFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg);
      bg_pkt[i] = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the PDV Frame in the message buffer
      bg_udp_chksum[i] = fg_pkt[i] + 60;
      bg_counter[i] = bg_pkt[i] + 70;
      bg_udp_chksum_start = ~*(uint16_t *)bg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
    }

    // naive sender version: it is simple and fast
    i=0; // increase maunally after each sending
    for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ) {	// Main cycle for the number of frames to send
      if ( sent_frames % n  < m ) {
        // foreground frame is to be sent
        *(uint64_t *)fg_counter[i] = sent_frames;			// set the counter in the frame 
        chksum = fg_udp_chksum_start + rte_raw_cksum(&sent_frames,8); 	// add the checksum of the counter to the initial checksum value
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);	// calculate 16-bit one's complement sum
        chksum = (~chksum) & 0xffff;					// make one's complement
        if (chksum == 0)						// checksum should not be 0 (0 means, no checksum is used)
           chksum = 0xffff;
        *(uint16_t *)fg_udp_chksum[i] = (uint16_t) chksum;		// set checksum in the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf[i], 1) ); 	// send foreground frame
        snd_ts[sent_frames] = rte_rdtsc();				// store timestamp
      } else {
        // background frame is to be sent
        *(uint64_t *)bg_counter[i] = sent_frames;			// set the counter in the frame 
        chksum = bg_udp_chksum_start + rte_raw_cksum(&sent_frames,8);   // add the checksum of the counter to the initial checksum value
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // calculate 16-bit one's complement sum
        chksum = (~chksum) & 0xffff;                                    // make one's complement
        if (chksum == 0)                                                // checksum should not be 0 (0 means, no checksum is used)
           chksum = 0xffff;
        *(uint16_t *)bg_udp_chksum[i] = (uint16_t) chksum;              // set checksum in the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf[i], 1) ); 	// send background frame
        snd_ts[sent_frames] = rte_rdtsc();				// store timestamp
      }
      i = (i+1) % N;
    } // this is the end of the sending cycle
  } // end of optimized code for single flow
  else {
    // optimized code for multiple flows: foreground and background frames are generated for each flow and pointers are stored in arrays
    // before sending, the frames are updated regarding counter and UDP checksum
    // N size arrays are used to resolve the write after send problem
    int j; // cycle variable for the above mentioned purpose: takes {0..N-1} values
    // num_dest_nets <= 256
    struct rte_mbuf *fg_pkt_mbuf[256][N], *bg_pkt_mbuf[256][N]; // pointers of message buffers for fg. and bg. PDV Frames
    uint8_t *fg_pkt[256][N], *bg_pkt[256][N]; // pointers to the frames (in the message buffers)
    uint8_t *fg_udp_chksum[256][N], *bg_udp_chksum[256][N], *fg_counter[256][N], *bg_counter[256][N];   // pointers to the given fields
    uint16_t fg_udp_chksum_start[256], bg_udp_chksum_start[256];  // starting values (uncomplemented checksums taken from the original frames)
    uint32_t chksum; // temporary variable for shecksum calculation
    uint32_t curr_dst_ipv4;     // IPv4 destination address, which will be changed
    in6_addr curr_dst_ipv6;     // foreground IPv6 destination address, which will be changed
    in6_addr curr_dst_bg;       // backround IPv6 destination address, which will be changed
    int i;                      // cycle variable for grenerating different destination network addresses
    if ( ip_version == 4 )
      curr_dst_ipv4 = *dst_ipv4;
    else // IPv6
      curr_dst_ipv6 = *dst_ipv6;
    curr_dst_bg = *dst_bg;

    // create PDV Test Frames
    for ( j=0; j<N; j++ ) {
      // create foreground PDV Frame (IPv4 or IPv6)
      for ( i=0; i<num_dest_nets; i++ ) {
        if ( ip_version == 4 ) {
          ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) i; // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
          fg_pkt_mbuf[i][j] = mkPdvFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4);
          fg_pkt[i][j] = rte_pktmbuf_mtod(fg_pkt_mbuf[i][j], uint8_t *); // Access the PDV Frame in the message buffer
          fg_udp_chksum[i][j] = fg_pkt[i][j] + 40;
          fg_counter[i][j] = fg_pkt[i][j] + 50;
        }
        else { // IPv6
          ((uint8_t *)&curr_dst_ipv6)[7] = (uint8_t) i; // bits 56 to 63 of the IPv6 address are rewritten, like in 2001:2:0:00xx::1
          fg_pkt_mbuf[i][j] = mkPdvFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, &curr_dst_ipv6);
          fg_pkt[i][j] = rte_pktmbuf_mtod(fg_pkt_mbuf[i][j], uint8_t *); // Access the PDV Frame in the message buffer
          fg_udp_chksum[i][j] = fg_pkt[i][j] + 60;
          fg_counter[i][j] = fg_pkt[i][j] + 70;
        }
        fg_udp_chksum_start[i] = ~*(uint16_t *)fg_udp_chksum[i][j]; // save the uncomplemented checksum value (same for all values of "j")
        // create backround Test Frame (always IPv6)
        ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
        bg_pkt_mbuf[i][j] = mkPdvFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg);
        bg_pkt[i][j] = rte_pktmbuf_mtod(bg_pkt_mbuf[i][j], uint8_t *); // Access the PDV Frame in the message buffer
        bg_udp_chksum[i][j] = bg_pkt[i][j] + 60;
        bg_counter[i][j] = bg_pkt[i][j] + 70;
        bg_udp_chksum_start[i] = ~*(uint16_t *)bg_udp_chksum[i][j]; // save the uncomplemented checksum value (same for all values of "j")
      }
    }

    // random number infrastructure is taken from: https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution
    // MT64 is used because of https://medium.com/@odarbelaeze/how-competitive-are-c-standard-random-number-generators-f3de98d973f0
    // thread_local is used on the basis of https://stackoverflow.com/questions/40655814/is-mersenne-twister-thread-safe-for-cpp
    thread_local std::random_device rd;  //Will be used to obtain a seed for the random number engine
    thread_local std::mt19937_64 gen(rd()); //Standard 64-bit mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<int> uni_dis(0, num_dest_nets-1);     // uniform distribution in [0, num_dest_nets-1]

    // naive sender version: it is simple and fast
    j=0; // increase maunally after each sending
    for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ 	// Main cycle for the number of frames to send
      int index = uni_dis(gen); // index of the pre-generated frame 
      if ( sent_frames % n  < m ) {
        *(uint64_t *)fg_counter[index][j] = sent_frames;                        // set the counter in the frame
        chksum = fg_udp_chksum_start[index] + rte_raw_cksum(&sent_frames,8);   	// add the checksum of the counter to the initial checksum value
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     	// calculate 16-bit one's complement sum
        chksum = (~chksum) & 0xffff;                                    	// make one's complement
        if (chksum == 0)                                                	// checksum should not be 0 (0 means, no checksum is used)
           chksum = 0xffff;
        *(uint16_t *)fg_udp_chksum[index][j] = (uint16_t) chksum;               // set checksum in the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    	// Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf[index][j], 1) );      // send foreground frame
        snd_ts[sent_frames] = rte_rdtsc();                              	// store timestamp
      } else {
        *(uint64_t *)bg_counter[index][j] = sent_frames;                        // set the counter in the frame
        chksum = bg_udp_chksum_start[index] + rte_raw_cksum(&sent_frames,8);   	// add the checksum of the counter to the initial checksum value
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     	// calculate 16-bit one's complement sum
        chksum = (~chksum) & 0xffff;                                    	// make one's complement
        if (chksum == 0)                                                	// checksum should not be 0 (0 means, no checksum is used)
           chksum = 0xffff;
        *(uint16_t *)bg_udp_chksum[index][j] = (uint16_t) chksum;               // set checksum in the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    	// Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf[index][j], 1) );      // send foreground frame
        snd_ts[sent_frames] = rte_rdtsc();                              	// store timestamp
      }
      j = (j+1) % N;
    } // this is the end of the sending cycle

  } // end of optimized code for multiple flows

  // Now, we check the time
  elapsed_seconds = (double)(rte_rdtsc()-start_tsc)/hz;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", side, elapsed_seconds);
  if ( elapsed_seconds > duration*TOLERANCE )
    rte_exit(EXIT_FAILURE, "%s sending exceeded the %3.10lf seconds limit, the test is invalid.\n", side, duration*TOLERANCE);
  printf("%s frames sent: %lu\n", side, sent_frames);
  return 0;
}

// Offsets from the start of the Ethernet Frame:
// EtherType: 6+6=12
// IPv6 Next header: 14+6=20, UDP Data for IPv6: 14+40+8=62
// IPv4 Protolcol: 14+9=23, UDP Data for IPv4: 14+20+8=42
int receivePdv(void *par) {
  // collecting input parameters:
  class receiverParametersPdv *p = (class receiverParametersPdv *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint8_t eth_id = p->eth_id;
  const char *side = p->side;
  uint64_t num_frames =  p->num_frames;
  uint16_t frame_timeout =  p->frame_timeout;
  uint64_t **receive_ts = p->receive_ts; 

  // further local variables
  int frames, i;
  struct rte_mbuf *pkt_mbufs[MAX_PKT_BURST]; // pointers for the mbufs of received frames
  uint16_t ipv4=htons(0x0800); // EtherType for IPv4 in Network Byte Order
  uint16_t ipv6=htons(0x86DD); // EtherType for IPv6 in Network Byte Order
  uint8_t identify[8]= { 'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y' };      // Identificion of the Test Frames
  uint64_t *id=(uint64_t *) identify;
  uint64_t received=0;  // number of received frames

  // prepare a NUMA local, cache line aligned array for reveive timestamps, and fill it with all 0-s
  uint64_t *rec_ts = (uint64_t *) rte_zmalloc(0, 8*num_frames, 128);
  if ( !rec_ts )
      rte_exit(EXIT_FAILURE, "Error: Receiver can't allocate memory for timestamps!\n");
  *receive_ts = rec_ts; // return the address of the array to the caller function

  while ( rte_rdtsc() < finish_receiving ){
    frames = rte_eth_rx_burst(eth_id, 0, pkt_mbufs, MAX_PKT_BURST);
    for (i=0; i < frames; i++){
      uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbufs[i], uint8_t *); // Access the PDV Frame in the message buffer
      // check EtherType at offset 12: IPv6, IPv4, or anything else
      if ( *(uint16_t *)&pkt[12]==ipv6 ) { /* IPv6 */
        /* check if IPv6 Next Header is UDP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if ( likely( pkt[20]==17 && *(uint64_t *)&pkt[62]==*id ) ) {
          // PDV frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          uint64_t counter = *(uint64_t *)&pkt[70]; 
          if ( unlikely ( counter >= num_frames ) )
            rte_exit(EXIT_FAILURE, "Error: PDV Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
          rec_ts[counter] = timestamp;
          received++; // also count it 
        }
      } else if ( *(uint16_t *)&pkt[12]==ipv4 ) { /* IPv4 */
        if ( likely( pkt[23]==17 && *(uint64_t *)&pkt[42]==*id ) ) {
          // Latency Frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          uint64_t counter = *(uint64_t *)&pkt[50];
          if ( unlikely ( counter >= num_frames ) )
            rte_exit(EXIT_FAILURE, "Error: PDV Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
          rec_ts[counter] = timestamp;
          received++; // also count it 
        }
      }
      rte_pktmbuf_free(pkt_mbufs[i]);
    }
  }
  if ( frame_timeout == 0 )
    printf("%s frames received: %lu\n", side, received); //  printed if normal PDV, but not printed if special throughput measurement is done
  return received;
}


void Pdv::measure(uint16_t leftport, uint16_t rightport) {
  uint64_t *left_send_ts, *right_send_ts, *left_receive_ts, *right_receive_ts; // pointers for timestamp arrays

  // set common parameters for senders
  senderCommonParameters scp(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc);

  if ( forward ) {      // Left to right direction is active

    // set individual parameters for the left sender

    // first, collect the appropriate values dependig on the IP versions
    ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
               &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

    // then, initialize the parameter class instance
    senderParametersPdv spars(&scp,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                              ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,&left_send_ts);

    // start left sender
    if ( rte_eal_remote_launch(sendPdv, &spars, cpu_left_sender) )
      std::cout << "Error: could not start Left Sender." << std::endl;

    // set parameters for the right receiver
    receiverParametersPdv rpars(finish_receiving,rightport,"Forward",duration*frame_rate,frame_timeout,&right_receive_ts);

    // start right receiver
    if ( rte_eal_remote_launch(receivePdv, &rpars, cpu_right_receiver) )
      std::cout << "Error: could not start Right Receiver." << std::endl;
  }

  if ( reverse ) {      // Right to Left direction is active

    // set individual parameters for the right sender

    // first, collect the appropriate values dependig on the IP versions
    ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
               &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

    // then, initialize the parameter class instance
    senderParametersPdv spars(&scp,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                              ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,&right_send_ts);

    // start right sender
    if (rte_eal_remote_launch(sendPdv, &spars, cpu_right_sender) )
      std::cout << "Error: could not start Right Sender." << std::endl;

    // set parameters for the left receiver
    receiverParametersPdv rpars(finish_receiving,leftport,"Reverse",duration*frame_rate,frame_timeout,&left_receive_ts);

    // start left receiver
    if ( rte_eal_remote_launch(receivePdv, &rpars, cpu_left_receiver) )
      std::cout << "Error: could not start Left Receiver." << std::endl;

  }

  std::cout << "Info: Testing started." << std::endl;

  // wait until active senders and receivers finish
  if ( forward ) {
    rte_eal_wait_lcore(cpu_left_sender);
    rte_eal_wait_lcore(cpu_right_receiver);
  }
  if ( reverse ) {
    rte_eal_wait_lcore(cpu_right_sender);
    rte_eal_wait_lcore(cpu_left_receiver);
  }

  // Process the timestamps
  int penalty=1000*duration+global_timeout; // latency to be reported for lost timestamps, expressed in milliseconds

  if ( forward )
    evaluatePdv(duration*frame_rate, left_send_ts, right_receive_ts, hz, frame_timeout, penalty, "Forward"); 
  if ( reverse )
    evaluatePdv(duration*frame_rate, right_send_ts, left_receive_ts, hz, frame_timeout, penalty, "Reverse"); 

  std::cout << "Info: Test finished." << std::endl;
}

senderParametersPdv::senderParametersPdv(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *side_,
                                                  struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                                  struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                                  uint16_t num_dest_nets_, uint64_t **send_ts_) :
  senderParameters(cp_,ip_version_,pkt_pool_,eth_id_,side_,dst_mac_,src_mac_,src_ipv4_,dst_ipv4_,src_ipv6_,dst_ipv6_,src_bg_,dst_bg_,num_dest_nets_) {
  send_ts = send_ts_;
}
    
receiverParametersPdv::receiverParametersPdv(uint64_t finish_receiving_, uint8_t eth_id_, const char *side_, 
				             uint64_t num_frames_, uint16_t frame_timeout_, uint64_t **receive_ts_) :
  receiverParameters(finish_receiving_,eth_id_,side_) {
  num_frames = num_frames_;
  frame_timeout = frame_timeout_;
  receive_ts = receive_ts_;
}

void evaluatePdv(uint64_t num_timestamps, uint64_t *send_ts, uint64_t *receive_ts, uint64_t hz, uint16_t frame_timeout, int penalty, const char *side) {
  int64_t frame_to = frame_timeout*hz/1000;	// exchange frame timeout from ms to TSC
  int64_t penalty_tsc = penalty*hz/1000;	// exchange penaly from ms to TSC
  int64_t PDV, Dmin, D99_9th_perc, Dmax;	// signed variable are used to prevent [-Wsign-compare] warning :-)
  uint64_t i;					// cycle variable
  int64_t *latency = new int64_t[num_timestamps]; // negative delay may occur, see the paper for details
  uint64_t num_corrected=0; 	// number of negative delay values corrected to 0
  uint64_t frames_lost=0;	// the number of physically lost frames

  if ( !latency )
    rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency values!\n");
  for ( i=0; i<num_timestamps; i++ ) {
    if ( receive_ts[i] ) {
      latency[i] = receive_ts[i]-send_ts[i]; 	// packet delay in TSC
      if ( unlikely ( latency[i] < 0 ) ) {
        latency[i] = 0;	// correct negative delay to 0
        num_corrected++;
      }
    } 
    else {
      frames_lost++; // frame physically lost
      latency[i] = penalty_tsc; // penalty of the lost timestamp
    }
  }
  if ( num_corrected )
    printf("Debug: %s number of negative delay values corrected to 0: %lu\n", side, num_corrected);
  if ( frame_timeout ) {
    // count the frames arrived in time
    uint64_t frames_received=0;
    for ( i=0; i<num_timestamps; i++ )
      if ( latency[i]<= frame_to )
        frames_received++;
    printf("%s frames received: %lu\n", side, frames_received);
    printf("Info: %s frames completely missing: %lu\n", side, frames_lost);
  } else {
    // calculate PDV
    // first, find Dmin
    Dmin = Dmax = latency[0];
    for ( i=1; i<num_timestamps; i++ ) {
      if ( latency[i] < Dmin )
        Dmin = latency[i]; 
      if ( latency[i] > Dmax )
        Dmax = latency[i]; 
      if ( latency[i] > penalty_tsc )
        printf("Debug: BUG: i=%lu, send_ts[i]=%lu, receive_ts[i]=%lu, latency[i]=%lu\n",i,send_ts[i],receive_ts[i],latency[i]);
    }
    // then D99_9th_perc
    std::sort(latency,latency+num_timestamps);
    D99_9th_perc = latency[int(ceil(0.999*num_timestamps))-1];
    PDV = D99_9th_perc - Dmin;
    printf("Info: %s D99_9th_perc: %lf\n", side, 1000.0*D99_9th_perc/hz);
    printf("Info: %s Dmin: %lf\n", side, 1000.0*Dmin/hz);
    printf("Info: %s Dmax: %lf\n", side, 1000.0*Dmax/hz);
    printf("%s PDV: %lf\n", side, 1000.0*PDV/hz);
  }
}
