// Activation functions
double NetMath::sigmoid(double value, bool prime, Neuron* neuron) {
    double val = 1 / (1+exp(-value));
    return prime ? val*(1-val)
                 : val;
}

// Cost Functions
double NetMath::meansquarederror (std::vector<double> calculated, std::vector<double> desired) {
    double error = 0.0;

    for (int v=0; v<calculated.size(); v++) {
        error += pow(calculated[v] - desired[v], 2);
    }

    return error / calculated.size();
}

// Weight update functions
double NetMath::vanillaupdatefn (int netInstance, double value, double deltaValue) {
    Network* net = Network::getInstance(netInstance);
    return value + net->learningRate * deltaValue;
}

double NetMath::gain(int netInstance, double value, double deltaValue, Neuron* neuron, int weightIndex) {

    Network* net = Network::getInstance(netInstance);
    double newVal = value + net->learningRate * deltaValue * (weightIndex < 0 ? neuron->biasGain : neuron->weightGain[weightIndex]);

    if ((newVal<=0 && value>0) || (newVal>=0 && value<0)) {
        if (weightIndex>-1) {
            neuron->weightGain[weightIndex] = fmax(neuron->weightGain[weightIndex]*0.95, 0.5);
        } else {
            neuron->biasGain = fmax(neuron->biasGain*0.95, 0.5);
        }
    } else {
        if (weightIndex>-1) {
            neuron->weightGain[weightIndex] = fmin(neuron->weightGain[weightIndex]+0.05, 5);
        } else {
            neuron->biasGain = fmin(neuron->biasGain+0.05, 5);
        }
    }

    return newVal;
}

// Other
std::vector<double> NetMath::softmax (std::vector<double> values) {
    double total = 0.0;

    for (int i=0; i<values.size(); i++) {
        total += values[i];
    }

    for (int i=0; i<values.size(); i++) {
        if (total) {
            values[i] /= total;
        }
    }

    return values;
}