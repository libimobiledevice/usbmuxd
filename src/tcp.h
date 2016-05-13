/*
* TCP header.
* Per RFC 793, September, 1981.
*/

typedef	u_long	tcp_seq;

#define	TH_ACK	0x10
#define	TH_SYN	0x02
#define	TH_RST	0x04

struct tcphdr {
	u_short	th_sport;		/* source port */
	u_short	th_dport;		/* destination port */
	tcp_seq	th_seq;			/* sequence number */
	tcp_seq	th_ack;			/* acknowledgement number */
	u_char	th_x2 : 4,		/* (unused) */
	th_off : 4;				/* data offset */
	u_char	th_flags;
	u_short	th_win;			/* window */
	u_short	th_sum;			/* checksum */
	u_short	th_urp;			/* urgent pointer */
};