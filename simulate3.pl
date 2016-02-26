#!/usr/local/bin/perl
use List::Util qw(first max maxstr min minstr reduce shuffle sum);
use Switch;

my $nStas = $ARGV[1];
my $nWifis = $ARGV[0];
my $rep = 1;
my $simulationTime = 5;
my $seed = -1; #Keep -1 to leave unchanged
my $stickiness = 0;
# my $EIFSnoDIFS = 1; #see collisions.numbers
# my $AckTimeout = 1; 
# my $frameMinFer = 0.1;
my $channelWidth = 40;
my $xDistanceFromAp = 10.0; #x component of maxWifiRange calculation

# 0 = default, line of nodes radiating from Ap, separated by 0.1 m
# 1 = 2 Aps, separated by 3*maxWifiRanges
# 2 = 3 Aps, separated like #1.
# 3 = 100 Aps, separated like #1.
my $defaultPositions = 2; #different experiments


my $eca = false;
my $hyst = false;
my $bitmap = false;
# my $verbose = false;
my $dynStick = false;
my $fairShare = false;
my $bitmap = false;
my $limitRange = false;
# my $srConservative = false;
# my $srResetMode = false;
my $elevenAc = true; #sets 802.11ac mcs
my @command = './waf --cwd=tmp3/ --run "scratch/eca-multiple-ap';

foreach (@ARGV){
	$eca = true
		if $_ eq '--eca';
	$hyst = true
		if $_ eq '--hyst';
	$bitmap = true
		if $_ eq '--bitmap';
	# $verbose = true
	# 	if $_ eq '--verbose';
	$dynStick = true
		if $_ eq '--dynStick';
	# $srResetMode = true
	# 	if $_ eq '--srResetMode';
	# $srConservative = true
	# 	if $_ eq '--srConservative';
	$fairShare = true
		if $_ eq '--fairShare';
	$elevenAc = true
		if $_ eq '--elevenAc';

	$limitRange = true
		if $_ eq '--limitRange';
}

#Modifying parameters according to test scenario
if ($defaultPositions > 0){
	switch ($defaultPositions){
		case 1 {
			$nStas = 4;
		}
		case 2 {
			$nStas = 4;
			$nWifis = 3;
		}
		case 3 {
			$nStas = 4;
			$nWifis = 100;
		}
	}
}

foreach my $j (1 .. $rep){
	$seed = $j
		if($rep > 1);
	my @addition = ("--nWifis=$nWifis
					--xDistanceFromAp=$xDistanceFromAp
					--seed=$seed 
					--nStas=$nStas
					--simulationTime=$simulationTime
					--elevenAc=$elevenAc
					--channelWidth=$channelWidth
					--eca=$eca
					--hyst=$hyst
					--bitmap=$bitmap
					--dynStick=$dynStick
					--fairShare=$fairShare
					--defaultPositions=$defaultPositions
					--limitRange=$limitRange\"");
	my @outPut = "@command @addition";
	print("###Simulating iteration $j of $rep\n");
	print ("@outPut\n");
	system(@outPut);
}


#Sending email at the end of the simulation
# my $simulation = "$simulationTime-$eca-$hyst-$stickiness-$dynStick-$bitmap-$srResetMode-$srConservative-$fairShare";
# my @mail = ("./sendMail.pl $simulation");
# system(@mail);
