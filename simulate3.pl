#!/usr/local/bin/perl
use List::Util qw(first max maxstr min minstr reduce shuffle sum);
my $nStas = $ARGV[0];
my $nWifis = $ARGV[1];
my $rep = 1;
my $simulationTime = 5;
my $seed = -1; #Keep -1 to leave unchanged
# my $stickiness = 1;
# my $EIFSnoDIFS = 1; #see collisions.numbers
# my $AckTimeout = 1; 
# my $frameMinFer = 0.1;
my $channelWidth = 20;
my $deltaWifiX = 10.0;


# my $verbose = false;
# my $eca = false;
# my $hyst = false;
# my $dynStick = false;
# my $fairShare = false;
# my $bitmap = false;
# my $srConservative = false;
# my $srResetMode = false;
my $elevenAc = true; #sets 802.11ac mcs
my @command = './waf --cwd=tmp3/ --run "scratch/eca-multiple-ap';

foreach (@ARGV){
	# $eca = true
	# 	if $_ eq '--eca';
	# $hyst = true
	# 	if $_ eq '--hyst';
	# $bitmap = true
	# 	if $_ eq '--bitmap';
	# $verbose = true
	# 	if $_ eq '--verbose';
	# $dynStick = true
	# 	if $_ eq '--dynStick';
	# $srResetMode = true
	# 	if $_ eq '--srResetMode';
	# $srConservative = true
	# 	if $_ eq '--srConservative';
	# $fairShare = true
	# 	if $_ eq '--fairShare';
	$elevenAc = true
		if $_ eq '--elevenAc';
}

foreach my $j (1 .. $rep){
	$seed = $j
		if($rep > 1);
	my @addition = ("--nWifis=$nWifis
					--deltaWifiX=$deltaWifiX
					--seed=$seed 
					--nStas=$nStas
					--simulationTime=$simulationTime
					--elevenAc=$elevenAc
					--channelWidth=$channelWidth\"");
	my @outPut = "@command @addition";
	print("###Simulating iteration $j of $rep\n");
	print ("@outPut\n");
	system(@outPut);
}


#Sending email at the end of the simulation
# my $simulation = "$simulationTime-$eca-$hyst-$stickiness-$dynStick-$bitmap-$srResetMode-$srConservative-$fairShare";
# my @mail = ("./sendMail.pl $simulation");
# system(@mail);
