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
my $stickiness = 0;
# my $EIFSnoDIFS = 1; #see collisions.numbers
# my $AckTimeout = 1; 
# my $frameMinFer = 0.1;
my $channelWidth = 20;
my $xDistanceFromAp = 10.0; #x component of maxWifiRange calculation

my $eca = false;
my $hyst = false;
# my $verbose = false;
my $dynStick = false;
my $fairShare = false;
my $bitmap = false;
my $limitRange = false; #if activated, no loss works, and range limitation is applied
my $srConservative = false;
my $srResetMode = false;
my $elevenAc = false; #sets 802.11ac mcs
my @command = './waf --cwd=tmp3/ --run "scratch/eca-multiple-ap';


#Modifying parameters according to test scenario
# 1 = Nodes are at the same location as AP
# 2 = Nodes form a cross around each AP
# 3 = Nodes are placed randomly around an AP [-delta,delta];
# 4 = HEW scenario by hand
# 5 = HEW scenario with the MobilityBuilding and other Building NS-3 classes
my $defaultPositions = 5;

switch ($scenario){

	case "single"{
		$rep = 1;
		$eca = false;
		$hyst = false;
		$fairShare = false;

		$simulationTime = 10;
		$defaultPositions = 5;
		$xDistanceFromAp = 5;
		
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

switch ($defaultPositions){
	case 1 {
		$nStas = 1;
		$nWifis = 2;
	}
	case 2 {
		$nStas = 4;
		$nWifis = 3;
		$limitRange = false;
	}
	case 3 {
		$nStas = 20;
		$nWifis = 10;
	}
	case 4 {
		$rep = 1;
		$simulationTime = 60;
		$nStas = 10;
		$nWifis = 50;
		$xDistanceFromAp = 5;
	}
	case 5 {
		$nStas = 10;
		$nWifis = 50;
		$xDistanceFromAp = 5;
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

#Now, process the files
my $outputFile = 'results3.log';
my @process = "cd ~/Dropbox/PhD/Research/NS3/ns-allinone-3.24.1/bake/source/ns-3.24/tmp3 && ./process3 $outputFile";
system(@process);

#Sending email at the end of the simulation
my $simulation = "$simulationTime-$eca-$hyst-$stickiness-$dynStick-$bitmap-$srResetMode-$srConservative-$fairShare";
my @mail = ("./sendMail.pl $scenario");
system(@mail)
	if ($scenario ne "single");
