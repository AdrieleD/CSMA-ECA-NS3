#!/usr/local/bin/perl
use warnings;
use strict;
use List::Util qw(first max maxstr min minstr reduce shuffle sum);
use Switch;

use constant false => 0;
use constant true  => 1;

my $scenario = $ARGV[0];
my $nStas = 1;
my $nWifis = 1;
my $rep = 1;
my $simulationTime = 25;
my $seed = -1; #Keep -1 to leave unchanged
my $stickiness = 1;
# my $EIFSnoDIFS = 1; #see collisions.numbers
# my $AckTimeout = 1; 
# my $frameMinFer = 0.1;
my $channelWidth = 20;
my $xDistanceFromAp = 10.0; #x component of maxWifiRange calculation

# 0 = default, line of nodes radiating from Ap, separated by 0.1 m
# 1 = 2 Aps, separated by beta*maxWifiRanges
# 2 = 3 Aps, separated like #1.
# 3 = 100 Aps, separated like #1.
# 4 = same as 2, but nodes are really close to their respective Aps
my $defaultPositions = 2;


my $eca = false;
my $hyst = false;
# my $verbose = false;
my $dynStick = false;
my $fairShare = false;
my $bitmap = false;
my $limitRange = false;
my $srConservative = false;
my $srResetMode = false;
my $elevenAc = false; #sets 802.11ac mcs
my @command = './waf --cwd=tmp3/ --run "scratch/eca-multiple-ap';


switch ($scenario){

	case "single"{
		$rep = 1;
		$eca = true;
		$hyst = true;
		$fairShare = true;
		$defaultPositions = 1;
		
	}
	case "DCF"{
		$stickiness = 0;
	}

	case "ECA1"{
		$eca = true;
		$hyst = true;
		$stickiness = 1;
	}

	case "ECA"{
		$eca = true;
		$hyst = true;
		$stickiness = 1;
		$fairShare = true;
	}

	case "ECA+"{
		$eca = true;
		$hyst = true;
		$stickiness = 1;
		$fairShare = true;
		$bitmap = true;
		$dynStick = true;
	}
}

#Modifying parameters according to test scenario
if ($defaultPositions > 0){
	switch ($defaultPositions){
		case 1 {
			$nStas = 10;
			$nWifis = 1;
		}
		case 2 {
			$nStas = 1;
			$nWifis = 3;
		}
		case 3 {
			$nStas = 4;
			$nWifis = 100;
		}
		case 4 {
			$nStas = 4;
			$nWifis = 3;
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
					--stickiness=$stickiness
					--defaultPositions=$defaultPositions
					--limitRange=$limitRange\"");
	my @outPut = "@command @addition";
	print("###Simulating iteration $j of $rep\n");
	print ("@outPut\n");
	system(@outPut);
}


#Sending email at the end of the simulation
my $simulation = "$simulationTime-$eca-$hyst-$stickiness-$dynStick-$bitmap-$srResetMode-$srConservative-$fairShare";
my @mail = ("./sendMail.pl $simulation");
# system(@mail)
# 	if ($scenario ne "single");
