
FCLayer::FCLayer (int netI, int s) : Layer(netI, s) {
    netInstance = netI;
    size = s;
    type = "FC";
    hasActivation = false;
}

FCLayer::~FCLayer (void) {
    for (int n=0; n<neurons.size(); n++) {
        delete neurons[n];
    }
}

void FCLayer::assignNext (Layer* l) {
    nextLayer = l;
}

void FCLayer::assignPrev (Layer* l) {
    prevLayer = l;
}

void FCLayer::init (int layerIndex) {
    for (int n=0; n<size; n++) {

        Neuron* neuron = new Neuron();

        if (layerIndex) {

            int weightsCount = 0;

            if (prevLayer->type == "FC") {
                weightsCount = prevLayer->size;
            } else if (prevLayer->type == "Conv") {
                weightsCount = prevLayer->filters.size() * prevLayer->outMapSize * prevLayer->outMapSize;
            } else {
                weightsCount = prevLayer->activations.size() * prevLayer->outMapSize * prevLayer->outMapSize;
            }

            neuron->weights = Network::getInstance(netInstance)->weightInitFn(netInstance, layerIndex, weightsCount);
            neuron->bias = 1;
        }

        neuron->init(netInstance);
        neurons.push_back(neuron);
    }
}

void FCLayer::forward (void) {

    Network* net = Network::getInstance(netInstance);

    for (int n=0; n<neurons.size(); n++) {

        neurons[n]->dropped = (double) rand() / (RAND_MAX) > net->dropout;

        if (net->isTraining && neurons[n]->dropped) {
            neurons[n]->activation = 0;

        } else {
            neurons[n]->sum = neurons[n]->bias;

            if (prevLayer->type == "FC") {
                for (int pn=0; pn<prevLayer->neurons.size(); pn++) {
                    neurons[n]->sum += prevLayer->neurons[pn]->activation * neurons[n]->weights[pn];
                }
            } else if (prevLayer->type == "Conv") {

                std::vector<double> activations = NetUtil::getActivations(prevLayer);

                for (int ai=0; ai<activations.size(); ai++) {
                    neurons[n]->sum += activations[ai] * neurons[n]->weights[ai];
                }
            } else {
                for (int c=0; c<prevLayer->channels; c++) {
                    for (int r=0; r<prevLayer->outMapSize; r++) {
                        for (int v=0; v<prevLayer->outMapSize; v++) {
                            neurons[n]->sum += prevLayer->activations[c][r][v] * neurons[n]->weights[c * prevLayer->outMapSize * prevLayer->outMapSize + r * prevLayer->outMapSize + v ];
                        }
                    }
                }
            }

            if (hasActivation) {
                neurons[n]->activation = activation(neurons[n]->sum, false, neurons[n]) / net->dropout;
            } else {
                neurons[n]->activation = neurons[n]->sum / net->dropout;
            }
        }
    }
}

void FCLayer::backward (std::vector<double> errors) {

    Network* net = Network::getInstance(netInstance);

    for (int n=0; n<neurons.size(); n++) {

        if (neurons[n]->dropped) {

            neurons[n]->error = 0;
            neurons[n]->deltaBias = 0;

        } else {

            if (errors.size()) {
                neurons[n]->error = errors[n];
            } else {
                if (hasActivation) {
                    neurons[n]->derivative = activation(neurons[n]->sum, true, neurons[n]);
                } else {
                    neurons[n]->derivative = 1;
                }

                double weightedErrors = 0.0;

                for (int nn=0; nn<nextLayer->neurons.size(); nn++) {
                    weightedErrors += nextLayer->neurons[nn]->error * nextLayer->neurons[nn]->weights[n];
                }

                neurons[n]->error = neurons[n]->derivative * weightedErrors;
            }

            if (prevLayer->type == "FC") {
                for (int wi=0; wi<neurons[n]->weights.size(); wi++) {
                    neurons[n]->deltaWeights[wi] += neurons[n]->error * prevLayer->neurons[wi]->activation;
                }

            } else {

                std::vector<double> activations = NetUtil::getActivations(prevLayer);

                for (int wi=0; wi<neurons[n]->weights.size(); wi++) {
                    neurons[n]->deltaWeights[wi] += neurons[n]->error * activations[wi] *
                        (1 + (net->l2 + net->l1)/(double)net->miniBatchSize * neurons[n]->deltaWeights[wi]);
                }
            }

            neurons[n]->deltaBias += neurons[n]->error;
        }
    }
}

void FCLayer::resetDeltaWeights (void) {
    for(int n=0; n<neurons.size(); n++) {

        neurons[n]->deltaBias = 0;

        for (int dw=0; dw<neurons[n]->deltaWeights.size(); dw++) {
            neurons[n]->deltaWeights[dw] = 0;
        }
    }
}


void FCLayer::applyDeltaWeights (void) {

    Network* net = Network::getInstance(netInstance);

    for(int n=0; n<neurons.size(); n++) {
        for (int dw=0; dw<neurons[n]->deltaWeights.size(); dw++) {
            if (net->l2) net->l2Error += 0.5 * net->l2 * pow(neurons[n]->weights[dw], 2);
            if (net->l1) net->l1Error += net->l1 * fabs(neurons[n]->weights[dw]);
        }
    }

    // Function pointers are far too slow for this
    // Using code repetitive switch statements makes a substantial perf difference
    // Doesn't mean I'm happy about it :(
    switch (net->updateFnIndex) {
        case 0: // vanilla
            for(int n=0; n<neurons.size(); n++) {
                for (int dw=0; dw<neurons[n]->deltaWeights.size(); dw++) {

                    double regularized = (neurons[n]->deltaWeights[dw]
                        + net->l2 * neurons[n]->weights[dw]
                        + net->l1 * (neurons[n]->weights[dw] > 0 ? 1 : -1)) / net->miniBatchSize;

                    neurons[n]->weights[dw] = NetMath::vanillaupdatefn(netInstance, neurons[n]->weights[dw], regularized);

                    if (net->maxNorm) net->maxNormTotal += neurons[n]->weights[dw] * neurons[n]->weights[dw];
                }
                neurons[n]->bias = NetMath::vanillaupdatefn(netInstance, neurons[n]->bias, neurons[n]->deltaBias);
            }
            break;
        case 1: // gain
            for(int n=0; n<neurons.size(); n++) {
                for (int dw=0; dw<neurons[n]->deltaWeights.size(); dw++) {

                    double regularized = (neurons[n]->deltaWeights[dw]
                        + net->l2 * neurons[n]->weights[dw]
                        + net->l1 * (neurons[n]->weights[dw] > 0 ? 1 : -1)) / net->miniBatchSize;

                    neurons[n]->weights[dw] = NetMath::gain(netInstance, neurons[n]->weights[dw], neurons[n]->deltaWeights[dw], neurons[n], dw);

                    if (net->maxNorm) net->maxNormTotal += neurons[n]->weights[dw] * neurons[n]->weights[dw];
                }
                neurons[n]->bias = NetMath::gain(netInstance, neurons[n]->bias, neurons[n]->deltaBias, neurons[n], -1);
            }
            break;
        case 2: // adagrad
            for(int n=0; n<neurons.size(); n++) {
                for (int dw=0; dw<neurons[n]->deltaWeights.size(); dw++) {

                    double regularized = (neurons[n]->deltaWeights[dw]
                                            + net->l2 * neurons[n]->weights[dw]
                                            + net->l1 * (neurons[n]->weights[dw] > 0 ? 1 : -1)) / net->miniBatchSize;

                    neurons[n]->weights[dw] = NetMath::adagrad(netInstance, neurons[n]->weights[dw], regularized, neurons[n], dw);

                    if (net->maxNorm) net->maxNormTotal += neurons[n]->weights[dw] * neurons[n]->weights[dw];
                }
                neurons[n]->bias = NetMath::adagrad(netInstance, neurons[n]->bias, neurons[n]->deltaBias, neurons[n], -1);
            }
            break;
        case 3: // rmsprop
            for(int n=0; n<neurons.size(); n++) {
                for (int dw=0; dw<neurons[n]->deltaWeights.size(); dw++) {

                    double regularized = (neurons[n]->deltaWeights[dw]
                                            + net->l2 * neurons[n]->weights[dw]
                                            + net->l1 * (neurons[n]->weights[dw] > 0 ? 1 : -1)) / net->miniBatchSize;

                    neurons[n]->weights[dw] = NetMath::rmsprop(netInstance, neurons[n]->weights[dw], regularized, neurons[n], dw);

                    if (net->maxNorm) net->maxNormTotal += neurons[n]->weights[dw] * neurons[n]->weights[dw];
                }
                neurons[n]->bias = NetMath::rmsprop(netInstance, neurons[n]->bias, neurons[n]->deltaBias, neurons[n], -1);
            }
            break;
        case 4: // adam
            for(int n=0; n<neurons.size(); n++) {
                for (int dw=0; dw<neurons[n]->deltaWeights.size(); dw++) {

                    double regularized = (neurons[n]->deltaWeights[dw]
                                            + net->l2 * neurons[n]->weights[dw]
                                            + net->l1 * (neurons[n]->weights[dw] > 0 ? 1 : -1)) / net->miniBatchSize;

                    neurons[n]->weights[dw] = NetMath::adam(netInstance, neurons[n]->weights[dw], regularized, neurons[n], dw);

                    if (net->maxNorm) net->maxNormTotal += neurons[n]->weights[dw] * neurons[n]->weights[dw];
                }
                neurons[n]->bias = NetMath::adam(netInstance, neurons[n]->bias, neurons[n]->deltaBias, neurons[n], -1);
            }
            break;
        case 5: // adadelta
            for(int n=0; n<neurons.size(); n++) {
                for (int dw=0; dw<neurons[n]->deltaWeights.size(); dw++) {

                    double regularized = (neurons[n]->deltaWeights[dw]
                                            + net->l2 * neurons[n]->weights[dw]
                                            + net->l1 * (neurons[n]->weights[dw] > 0 ? 1 : -1)) / net->miniBatchSize;

                    neurons[n]->weights[dw] = NetMath::adadelta(netInstance, neurons[n]->weights[dw], regularized, neurons[n], dw);

                    if (net->maxNorm) net->maxNormTotal += neurons[n]->weights[dw] * neurons[n]->weights[dw];
                }
                neurons[n]->bias = NetMath::adadelta(netInstance, neurons[n]->bias, neurons[n]->deltaBias, neurons[n], -1);
            }
            break;
    }

    if (net->maxNorm) {
        net->maxNormTotal = sqrt(net->maxNormTotal);
        NetMath::maxNorm(netInstance);
    }
}
